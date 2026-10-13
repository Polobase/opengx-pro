/* Generator links USART1 (G1, board A) and USART2 (G2, board B): the ':' line protocol of
 * docs/spec/protocols.md §2-§3 and §5.1, application.md §7.
 *
 * Receive runs in the UART interrupt, byte by byte, like the original ISRs 0x0800C758 /
 * 0x0800CF90: values of :g/:p uploads go straight into the channel's active program in RAM
 * (several KB per line, never buffered), :w arguments into a per-link array, :n text into a
 * 63-byte buffer. A complete line sets UIF_LINE(ch); from then on that link discards bytes until
 * link_process() has handled the line (the host waits for the reply). The PC-mode mirror
 * commands w11/w14/w15/w24/w25 are applied directly in the interrupt.
 *
 * Deviations from the original are marked DEVIATION. The wire format is unchanged: '.' also
 * separates values, digits are taken as (c & 0x0F) without validation, an empty value means
 * all-ones, a value must be followed by ',' (the token before CR/LF is ignored), a token of
 * more than 26 characters drops the line, and w frames are never answered. */
#include "link.h"

#include <string.h>

#include "fmt.h"
#include "link_hw.h"
#include "link_internal.h"
#include "state.h"
#include "storage.h"
#include "uart.h"

#ifdef LINK_RX_TAP
void LINK_RX_TAP(unsigned ch, uint8_t c);
#endif
#ifdef LINK_OK_HOOK
void LINK_OK_HOOK(unsigned ch);
#endif

#define RX_TEXT_MAX  63u   /* b[5..0x43] of the original 0x44-byte line buffer */
#define RX_NAME_MAX  59u   /* a text byte arriving at b[0x40] drops the line */
#define RX_TOKEN_MAX 26u
#define RX_WARGS     32u
#define RX_G_VALUES  (2u * PROGRAM_STEPS)   /* 400 */
#define RX_P_VALUES  (5u + PROGRAM_STEPS)   /* 205 */

/* Three corner cases of malformed lines, handled as the original's receive interrupts handle them
 * (found by comparing the parts of 0x0800C758 / 0x0800CF90 and of the frame handlers that no
 * scenario runs; checked against the original on the emulator, scenario serial_malformed): the
 * value index survives an :n frame, an index carried past another letter's array keeps counting,
 * and text after a CR that follows 59 characters is kept. APP=nx keeps its own, more forgiving
 * rules on purpose (DEVIATIONS.md, nx): every frame it handles starts the next one at index 0, an
 * :n frame included, so a line cut off by noise cannot shift the next upload; like the classic
 * build it stores nothing past an array. */
#ifdef APP_NX
#define LINK_RX_EXACT 0
#else
#define LINK_RX_EXACT 1
#endif

/* pc_flags bits holding the last w15 values (never read by the original) */
#define PCF_W15(ch, k) (0x40u << ((ch) * 2u + (k)))

/* Magic first arguments of the maintenance commands (protocols.md §3.2) */
#define W60_KEY  1112111u
#define W62_KEY  3336333u
#define W63_UP   111999u
#define W63_DOWN 111000u
#define W64_ON   18888881u
#define W64_OFF  10000001u
#define W80_KEY  13583792u
#define W95_KEY  12021u
#define W96_KEY  12321u
#define W97_KEY  12621u

enum rx_state { RX_IDLE, RX_LETTER, RX_NUM1, RX_NUM2, RX_EQUALS, RX_PAYLOAD };

struct link_rx {
    uint8_t state;
    uint8_t letter;     /* 'w', 'p', 'g' or 'n' (b[1]) */
    uint8_t num[2];     /* the two number characters & 0x0F (b[2], b[3]) */
    uint8_t ok;         /* ":ok" detector */
    uint8_t tok_len;    /* characters of the current value */
    uint8_t text_len;   /* bytes in text[] (n frames) */
    uint16_t idx;       /* next value index */
    uint64_t acc;       /* current value */
    char text[RX_TEXT_MAX];
    uint64_t wargs[RX_WARGS];
};

static struct link_rx rx[2];
static volatile uint8_t gen_ok;   /* gen_ok_state 0x2000000B, shared by both links */

void link_init(void)
{
    memset(rx, 0, sizeof rx);
    gen_ok = 0;
    factory_reset();
}

int link_gate_open(void)
{
    return (g_ui_flags & UIF_PC_UNLOCK) || !pin_read(GPIOB, HOST_DETECT_PIN);
}

static unsigned frame_number(const struct link_rx *r)
{
    return r->num[0] * 10u + r->num[1];
}

void link_frame_tag(unsigned ch, char out[4])
{
    const struct link_rx *r = &rx[ch & 1u];
    out[0] = r->letter ? (char)r->letter : '-';
    out[1] = (char)('0' + r->num[0] % 10u);
    out[2] = (char)('0' + r->num[1] % 10u);
    out[3] = 0;
}

/* ------------------------------------------------------------------------------------------ */
/* Receive (interrupt context)                                                                */

static uint64_t *program_word(struct program *p, unsigned i)
{
    return i < 5u ? &p->hdr[i] : &p->freq[i - 5u];
}

#if LINK_RX_EXACT
static void store_value(unsigned ch, struct link_rx *r, uint64_t v)
{
    unsigned last = r->letter == 'g' ? RX_G_VALUES - 1u : r->letter == 'p' ? RX_P_VALUES - 1u : RX_WARGS - 1u;
    /* DEVIATION: a value whose index lies past this letter's array (an index left by an aborted
     * frame of a longer type, see link_rx) is not stored. The original writes it into the RAM
     * behind the array (G1's :p values land in G2's program, 0x0800C9D2 / 0x0800C8F2). */
    if (r->idx <= last) {
        if (r->letter == 'g')
            g_ch[ch].params[r->idx] = (uint16_t)v;
        else if (r->letter == 'p')
            *program_word(&g_ch[ch].prog, r->idx) = v;
        else
            r->wargs[r->idx] = v;
    }
    /* The original tests for equality: further values overwrite the last element, and an index
     * that is already past it keeps counting. */
    if (r->idx != last)
        r->idx++;
}
#else
static void store_value(unsigned ch, struct link_rx *r, uint64_t v)
{
    unsigned last = r->letter == 'g' ? RX_G_VALUES - 1u : r->letter == 'p' ? RX_P_VALUES - 1u : RX_WARGS - 1u;
    if (r->idx > last)   /* an index left by a dropped frame of a longer type: nothing is stored */
        return;
    if (r->letter == 'g')
        g_ch[ch].params[r->idx] = (uint16_t)v;
    else if (r->letter == 'p')
        *program_word(&g_ch[ch].prog, r->idx) = v;
    else
        r->wargs[r->idx] = v;
    if (r->idx < last)   /* further values overwrite the last element, as in the original */
        r->idx++;
}
#endif

static void mirror_w11(unsigned ch, const uint64_t *a)   /* 0x0800CBF0 / 0x0800D428 */
{
    for (unsigned out = 0; out < 2u; out++) {
        if (a[out] == 1u)
            g_misc_flags |= (uint16_t)MISC_OUT_EN(ch, out);
        else if (a[out] == 0u)
            g_misc_flags &= (uint16_t)~MISC_OUT_EN(ch, out);
        else
            continue;
        g_pc_flags &= (uint16_t)~PCF_FREQ(ch, out);
    }
}

static void mirror_w14(unsigned ch, const uint64_t *a)   /* 0x0800CEAE / 0x0800D6EE */
{
    if (a[0] == 0u)
        g_ui_flags &= (uint16_t)~UIF_SYNC(ch);
    else if (a[0] == 1u)
        g_ui_flags |= (uint16_t)UIF_SYNC(ch);
    g_pc_flags &= (uint16_t)~(PCF_FREQ(ch, 0) | PCF_FREQ(ch, 1));
}

static void mirror_w15(unsigned ch, const uint64_t *a)   /* 0x0800CCDC / 0x0800D516 */
{
    if (a[0] == 0u)
        g_pc_flags &= (uint16_t)~PCF_W15(ch, 0);
    else if (a[0] == 1u)
        g_pc_flags |= (uint16_t)PCF_W15(ch, 0);
    int b6 = (g_pc_flags & PCF_W15(ch, 0)) != 0;
    if (g_ui_flags & UIF_SYNC(ch)) {
        if (b6)
            g_pc_flags |= (uint16_t)PCF_W15(ch, 1);
        else
            g_pc_flags &= (uint16_t)~PCF_W15(ch, 1);
    } else if (a[1] == 0u) {
        g_pc_flags &= (uint16_t)~PCF_W15(ch, 1);
    } else if (a[1] == 1u) {
        g_pc_flags |= (uint16_t)PCF_W15(ch, 1);
    }
    g_pc_flags &= (uint16_t)~(PCF_FREQ(ch, 0) | PCF_FREQ(ch, 1));
}

/* Immediate part of a complete w line (end of 0x0800C758 / 0x0800CF90). Deferred numbers keep
 * the line pending for link_process(); all others are handled or dropped here. */
static void w_immediate(unsigned ch, struct link_rx *r)
{
    const uint64_t *a = r->wargs;
    struct channel *c = &g_ch[ch];
    switch (frame_number(r)) {
    case 0:
    case 16:
    case 63:
    case 64:
    case 80:
    case 91:
    case 92:
    case 95:
    case 96:
    case 97:
        return;
    case 60:   /* USART2 only */
        if (ch == 1u)
            return;
        break;
    case 62:   /* USART1 only */
        if (ch == 0u)
            return;
        break;
    case 11:
    case 14:
    case 15:
    case 24:
    case 25:
        if (!link_gate_open())
            return;   /* stays pending; link_process drops it */
        switch (frame_number(r)) {
        case 11:
            mirror_w11(ch, a);
            break;
        case 14:
            mirror_w14(ch, a);
            break;
        case 15:
            mirror_w15(ch, a);
            break;
        case 24:
            if (g_ui_flags & UIF_SYNC(ch)) {
                c->pc_freq[1] = a[0];
                g_pc_flags &= (uint16_t)~PCF_FREQ(ch, 1);
            }
            c->pc_freq[0] = a[0];
            g_pc_flags &= (uint16_t)~PCF_FREQ(ch, 0);
            break;
        default:   /* 25 */
            /* DEVIATION: in sync mode the line is dropped here; the original left it pending
             * for the main loop, which only dropped it (the link lost bytes meanwhile). */
            if (!(g_ui_flags & UIF_SYNC(ch))) {
                c->pc_freq[1] = a[0];
                g_pc_flags &= (uint16_t)~PCF_FREQ(ch, 1);
            }
            break;
        }
        break;
    default:
        break;
    }
    r->idx = 0;
    g_ui_flags &= (uint16_t)~UIF_LINE(ch);
}

static void line_done(unsigned ch, struct link_rx *r)
{
    r->state = RX_IDLE;
    g_ui_flags |= (uint16_t)UIF_LINE(ch);
    if (r->letter == 'w')
        w_immediate(ch, r);
}

static void rx_values(unsigned ch, struct link_rx *r, uint8_t c)   /* g, p, w */
{
    if (c == ',' || c == '.') {
        store_value(ch, r, r->tok_len ? r->acc : UINT64_MAX);
        r->acc = 0;
        r->tok_len = 0;
    } else if (c == '\n') {
        line_done(ch, r);
    } else if (r->tok_len >= RX_TOKEN_MAX) {
        r->state = RX_IDLE;   /* over-long value: line dropped */
    } else {
        r->acc = r->acc * 10u + (c & 0x0Fu);   /* g values wrap at 16 bits when stored */
        r->tok_len++;
    }
}

#if LINK_RX_EXACT
static void rx_text(unsigned ch, struct link_rx *r, uint8_t c)   /* n */
{
    if (c == '\n') {
        memset(&r->text[r->text_len], 0, RX_TEXT_MAX - r->text_len);
        line_done(ch, r);
    } else if (c != '\r' && r->text_len == RX_NAME_MAX) {
        /* a character arriving at b[0x40], directly after 59 others: line dropped (0x0800CB30).
         * Nothing else drops an :n line: a CR is stored there, and text after it is kept. */
        r->state = RX_IDLE;
    } else if (r->text_len < RX_TEXT_MAX) {
        r->text[r->text_len++] = c == '\r' ? 0 : (char)c;
    }
    /* DEVIATION: what does not fit the buffer (b[0x44] onwards) is left out and the line goes on;
     * the original writes it into the RAM behind the buffer (quirk 9). */
}
#else
static void rx_text(unsigned ch, struct link_rx *r, uint8_t c)   /* n */
{
    if (c == '\n') {
        memset(&r->text[r->text_len], 0, RX_TEXT_MAX - r->text_len);
        line_done(ch, r);
    } else if (c == '\r') {
        /* DEVIATION: CRs are bounded by the buffer; the original wrote past it (quirk 9). */
        if (r->text_len < RX_TEXT_MAX)
            r->text[r->text_len++] = 0;
        else
            r->state = RX_IDLE;
    } else if (r->text_len < RX_NAME_MAX) {
        r->text[r->text_len++] = (char)c;
    } else {
        r->state = RX_IDLE;   /* more than 59 characters: line dropped */
    }
}
#endif

static void link_rx(unsigned ch, uint8_t c)
{
    struct link_rx *r = &rx[ch];

    /* ":ok" from a generator board. DEVIATION: detected by its own matcher, also while a line
     * is pending (the original lost it then, quirk 1), and only as ':' 'o' 'k' (the original
     * accepted any 'k' in state 2 and parsed ":o.." further as a frame of the previous letter). */
    uint8_t ok = r->ok;
    r->ok = 0;
    if (c == ':' && r->state == RX_IDLE)
        r->ok = 1;
    else if (c == 'o' && ok == 1u)
        r->ok = 2;
    else if (c == 'k' && ok == 2u) {
        gen_ok = 1;
#ifdef LINK_OK_HOOK
        LINK_OK_HOOK(ch);
#endif
    }

    if (g_ui_flags & UIF_LINE(ch))
        return;   /* line pending: byte discarded (original behaviour) */

    switch (r->state) {
    case RX_IDLE:
        if (c == ':')
            r->state = RX_LETTER;
        break;
    case RX_LETTER:
        if (c == 'w' || c == 'p' || c == 'g' || c == 'n') {
            r->letter = c;
            r->state = RX_NUM1;
        } else {
            r->state = RX_IDLE;
        }
        break;
    case RX_NUM1:
    case RX_NUM2:
        r->num[r->state - RX_NUM1] = c & 0x0Fu;   /* not checked to be a digit (original) */
        r->state++;
        break;
    case RX_EQUALS:
        if (c != '=') {
            r->state = RX_IDLE;
            break;
        }
        r->state = RX_PAYLOAD;
        /* 1:1 with the original: the value index is reset only after a :w, :p or :g frame is
         * *handled* (w_immediate, link_process), never at the start of a frame and not by an :n
         * frame (APP=nx: by every handled frame). So an aborted frame leaves the index where it
         * was and the next frame's values continue from there, and :w argument slots keep the
         * values of an earlier :w frame.
         * Within one letter the index stops at its last element (g 399, p 204, w 31); carried
         * into a frame of a shorter type it can lie past that array (store_value). Only the
         * within-value accumulator is cleared here. */
        r->acc = 0;
        r->tok_len = 0;
        r->text_len = 0;
        break;
    default:
        if (r->letter == 'n')
            rx_text(ch, r, c);
        else
            rx_values(ch, r, c);
        break;
    }
}

int uart_rx_hook(enum uart_port port, uint8_t c)
{
    if (port == UART_FACTORY) {
        factory_rx(c);
    } else {
        unsigned ch = port == UART_GEN_B ? 1u : 0u;
#ifdef LINK_RX_TAP
        LINK_RX_TAP(ch, c);
#endif
        link_rx(ch, c);
    }
    return 1;
}

int link_rx_idle(unsigned ch)
{
    /* volatile: rx[ch].state is written by the UART interrupt, so an inlined call (e.g. under
     * -flto) must not hoist this load out of a polling loop. */
    return *(volatile uint8_t *)&rx[ch & 1u].state == RX_IDLE;
}

/* ------------------------------------------------------------------------------------------ */
/* Transmit (main loop only)                                                                  */

static enum uart_port link_port(unsigned ch)
{
    return ch ? UART_GEN_B : UART_GEN_A;
}

static void tx(unsigned ch, const char *s, const char *end)
{
    while (s < end)
        uart_putc(link_port(ch), (uint8_t)*s++);
}

static void tx_str(unsigned ch, const char *s)
{
    uart_puts(link_port(ch), s);
}

/* The original polls TC after every byte, so a send returns when the line is on the wire. */
static void tx_end(unsigned ch)
{
    uart_flush(link_port(ch));
}

static char *put_w_head(char *p, unsigned id)   /* ":wNN=" */
{
    *p++ = ':';
    *p++ = 'w';
    p = fmt_put_fixed(p, id, 2);
    *p++ = '=';
    return p;
}

void link_send_line(unsigned ch, const char *s)   /* uart1_send_line 0x0800C268 / 0x0800C4AC */
{
    tx_str(ch, s);
    tx_str(ch, "\r\n");
    tx_end(ch);
}

void link_send_raw(unsigned ch, const char *buf, unsigned len)
{
    tx(ch, buf, buf + len);
    tx_end(ch);
}

unsigned link_fmt_w_u64(char *buf, unsigned id, uint64_t v)
{
    char *p = put_w_head(buf, id);
    p = fmt_put_u64(p, v);
    memcpy(p, ",\r\n", 3);
    return (unsigned)(p + 3 - buf);
}

unsigned link_fmt_w_pair5(char *buf, unsigned id, unsigned a, unsigned b)
{
    char *p = put_w_head(buf, id);
    p = fmt_put_fixed(p, a, 5);
    *p++ = ',';
    p = fmt_put_fixed(p, b, 5);
    memcpy(p, ",\r\n", 3);
    return (unsigned)(p + 3 - buf);
}

void link_send_w_u64(unsigned ch, unsigned id, uint64_t v)   /* 0x0800C084 / 0x0800C2C8 */
{
    char buf[32];
    link_send_raw(ch, buf, link_fmt_w_u64(buf, id, v));
}

void link_send_w_pair5(unsigned ch, unsigned id, unsigned a, unsigned b)   /* 0x0800C140 / 0x0800C384 */
{
    char buf[20];
    link_send_raw(ch, buf, link_fmt_w_pair5(buf, id, a, b));
}

/* DEVIATION: an index >= 100 (only possible after a bogus factory ':b') is sent as its last
 * two digits; the original produced a non-digit character. */
void link_send_cal_value(unsigned idx, unsigned v)   /* uart1_send_cal_value 0x0800BFA8 */
{
    char buf[24], *p = buf;
    memcpy(p, ":w60=1112111,", 13);
    p = fmt_put_fixed(p + 13, idx, 2);
    *p++ = ',';
    p = fmt_put_fixed(p, v & 0xFFFFu, 5);   /* the table entry as u16, as the original (ldrh) */
    memcpy(p, ",\r\n", 3);
    tx(0, buf, p + 3);
    tx_end(0);
}

/* gen_wait_ok 0x08012524: up to 200 000 polls of ~12-14 cycles (~40 ms at 64 MHz), timeout
 * ignored, then delay_loop(0x855). Here: 1000 slices of 640 delay_loop iterations (4 cycles
 * each, measured) = 40 ms, independent of interrupts and compiler output. */
#define WAIT_OK_SLICES 1000u
#define WAIT_OK_SLICE  640u

void link_wait_ok(void)
{
    gen_ok = 0;
    for (unsigned i = 0; i < WAIT_OK_SLICES && !gen_ok; i++)
        delay_loop(WAIT_OK_SLICE);
    delay_loop(0x855);
}

/* ------------------------------------------------------------------------------------------ */
/* Line handling (main loop): uart1_frame_handler 0x0800F22C / uart2_frame_handler 0x0800F9C4 */

static void reply_ok(unsigned ch)
{
    link_send_line(ch, ":ok");
}

static void reply_err(unsigned ch)
{
    link_send_line(ch, ":err");
}

static char *put_n_head(char *p, char letter, const struct link_rx *r)   /* ":xNN=" echoing NN */
{
    *p++ = ':';
    *p++ = letter;
    *p++ = (char)(r->num[0] | 0x30u);
    *p++ = (char)(r->num[1] | 0x30u);
    *p++ = '=';
    return p;
}

/* program_dump_g1/g2 0x08010DB0 / 0x0801101C: the stored record, header + count frequencies. */
static void dump_program(unsigned ch, unsigned slot)
{
    uint64_t hdr[5];
    storage_slot_read(slot, 0, hdr, sizeof hdr);
    if (hdr[HDR_COUNT] >= 1u && hdr[HDR_COUNT] <= PROGRAM_STEPS) {
        unsigned n = 5u + (unsigned)hdr[HDR_COUNT];
        for (unsigned i = 0; i < n; i++) {
            uint64_t v = i < 5u ? hdr[i] : 0;
            if (i >= 5u)
                storage_slot_read(slot, 8u * i, &v, sizeof v);
            char buf[24], *p = fmt_put_u64(buf, v);
            *p++ = ',';
            tx(ch, buf, p);
        }
    }
    tx_str(ch, "\r\n");
}

/* steptable_dump_g1/g2 0x08010BE8 / 0x08010CCC: the stored step parameters.
 * Like the original, only the first 200 of the 400 values are sent (quirk 7): kept on purpose,
 * because PC software may depend on this reply's exact format. */
#define DUMP_G_VALUES 200u
static void dump_params(unsigned ch, unsigned slot)
{
    uint16_t g[40];
    storage_slot_read(slot, PROG_PARAMS_OFFSET, g, 2);
    if (g[0] != 0xFFFFu) {
        for (unsigned i = 0; i < DUMP_G_VALUES; i += 40u) {
            storage_slot_read(slot, PROG_PARAMS_OFFSET + 2u * i, g, sizeof g);
            for (unsigned k = 0; k < 40u; k++) {
                char buf[8], *p = fmt_put_u64(buf, g[k]);
                *p++ = ',';
                tx(ch, buf, p);
            }
        }
    }
    tx_str(ch, "\r\n");
}

static void reply_name(unsigned ch, unsigned slot, const struct link_rx *r)
{
    char buf[64], *p = put_n_head(buf, 'n', r);
    for (unsigned i = 0; i < 57u; i++) {
        uint8_t c = (uint8_t)g_names[slot][i];
        if (c < 0x20u || c > 0x7Fu)
            break;
        *p++ = (char)c;
    }
    memcpy(p, "\r\n", 2);
    tx(ch, buf, p + 2);
}

static void process_n(unsigned ch, struct link_rx *r)
{
    unsigned slot = frame_number(r);
    char sub = r->text[0];
    if ((!link_gate_open() && sub != '$') || slot > PROGRAM_SLOTS) {
        reply_err(ch);
        return;
    }
    if (ch == 1u && slot == 0u)
        slot = NAME_SLOTS - 1u;   /* USART2: slot 00 is the G2 PC name, slot 31 */

    char buf[8];
    switch (sub) {
    case '*':
        tx(ch, buf, put_n_head(buf, 'p', r));
        dump_program(ch, slot);
        break;
    case '#':
        tx(ch, buf, put_n_head(buf, 'g', r));
        dump_params(ch, slot);
        break;
    case '$':
        tx_str(ch, ":Rev201\r\n");
        break;
    case ',':
        program_erase(slot);   /* the name stays (original) */
        flags_clear(&g_pc_flags, PCF_NAME(ch));
        reply_ok(ch);
        return;
    case '?':
        reply_name(ch, slot, r);
        break;
    default:
        name_set(slot, r->text, RX_TEXT_MAX);
        if (slot >= 1u && slot <= PROGRAM_SLOTS)
            names_save();
        flags_clear(&g_pc_flags, PCF_NAME(ch));
        reply_ok(ch);
        return;
    }
    tx_end(ch);
}

static void process_w(unsigned ch, struct link_rx *r)
{
    const uint64_t *a = r->wargs;
    struct channel *c = &g_ch[ch];
    uint16_t v;

    switch (frame_number(r)) {
    case 0:
        /* DEVIATION: acknowledged without effect. The original programs a write-once word
         * into the bootloader page (0x08003F00), which this firmware never writes. */
        break;
    case 60:   /* USART2 only */
        /* DEVIATION: uses this link's own arguments (the original read USART1's, quirk 5)
         * and range-checks the index. misc_flags = 0 is original behaviour. */
        if (ch == 1u && a[0] == W60_KEY && a[1] < 32u)
            g_cal_table[a[1]] = (int16_t)a[2];
        if (ch == 1u)
            flags_clear(&g_misc_flags, 0xFFFFu);
        break;
    case 62:   /* USART1 only */
        if (ch == 0u && link_gate_open() && a[0] == W62_KEY)
            cal_restore_backup();
        break;
    case 63:
        if (!link_gate_open())
            break;
        v = (uint16_t)g_cal_table[0];   /* compared unsigned, as the original */
        if (a[0] == W63_UP) {
            g_cal_table[0] = (int16_t)(v < 32100u ? v + 1u : 32100u);
            cal_save(0);
        } else if (a[0] == W63_DOWN) {
            g_cal_table[0] = (int16_t)(v <= 31900u ? 31900u : v - 1u);
            cal_save(0);
        }
        break;
    case 64:
        if (a[0] == W64_ON)
            g_backlight_duty = 8;
        else if (a[0] == W64_OFF)
            g_backlight_duty = 0;
        break;
    case 80:
        if (a[0] == W80_KEY)
            for (unsigned i = 0; i < 21u; i++)
                g_cal_table[i] = (int16_t)a[i + 1u];
        break;
    case 91:
        /* Excluded vendor unlock handshake: any value is accepted, the PC icon stays unlocked
         * (UIF_PC_UNLOCK is never cleared); only the original's PB2 = 0 remains.
         * DEVIATION: the icon is not drawn here (the original drew it at (4,5) in any UI state
         * on its magic values); the PC-mode screen always shows it unlocked. */
        pin_low(GPIOB, HANDSHAKE_PIN);
        break;
    case 92:
        delay_loop(0x34155);
        pin_high(GPIOB, HANDSHAKE_PIN);
        delay_loop(0x34155);
        link_send_line(ch, ":w91=0,");
        break;
    case 95:
        if (a[0] == W95_KEY)
            NVIC_SystemReset();
        break;
    case 96:
        if (!(g_ui_flags & UIF_ERASED_ALL) && a[0] == W96_KEY) {   /* once per power cycle */
            storage_erase_all();
            flags_set(&g_ui_flags, UIF_ERASED_ALL);
        }
        break;
    case 97:
        if (a[0] == W97_KEY && link_gate_open()) {
            c->pc_freq[0] = 0;
            c->pc_freq[1] = 0;
            flags_set(&g_ui_flags, UIF_SYNC(ch));
            /* DEVIATION: also marks both PC names for redraw, since they are cleared here. */
            flags_clear(&g_pc_flags, (uint16_t)(PCF_FREQ(ch, 0) | PCF_FREQ(ch, 1) | PCF_W15(ch, 0)
                                                | PCF_W15(ch, 1) | PCF_NAME(0) | PCF_NAME(1)));
            memset(g_names[0], 0, NAME_LEN);
            memset(g_names[NAME_SLOTS - 1u], 0, NAME_LEN);
        }
        break;
    default:   /* w16, or a mirror command left pending by a closed gate */
        break;
    }
}

void link_process(unsigned ch)
{
    if (ch > 1u || !(g_ui_flags & UIF_LINE(ch)))
        return;
    LINK_BARRIER();
    struct link_rx *r = &rx[ch];
    unsigned slot = frame_number(r);

    switch (r->letter) {
    case 'g':   /* values are already in the active step-parameter table */
        if (!link_gate_open() || slot == 0u || slot > PROGRAM_SLOTS) {
            reply_err(ch);
        } else {
            g_ch[ch].step_sent = 0xFF;
            reply_ok(ch);
        }
        break;
    case 'p':   /* values are already in the active program record */
        if (!link_gate_open() || slot == 0u || slot > PROGRAM_SLOTS) {
            reply_err(ch);
        } else {
            program_save(slot, ch);
            g_ch[ch].step_sent = 0xFF;
            reply_ok(ch);
        }
        break;
    case 'n':
        process_n(ch, r);
        break;
    default:
        process_w(ch, r);
        break;
    }
#if LINK_RX_EXACT
    if (r->letter != 'n')   /* the original's :n exits (0x0800F960 / 0x08010122) leave the index */
#endif
    r->idx = 0;
    LINK_BARRIER();
    flags_clear(&g_ui_flags, UIF_LINE(ch));
}
