/**
 * A few display commands for the live demo, over the core Web Serial transport of
 * `@freqgen/core`. Only commands the display firmware answers itself are used: the version query,
 * the PC-mode title, the backlight and reading stored programs. Nothing here starts a session of
 * the PC software or touches the generator outputs.
 */

import { WebSerialTransport } from "@freqgen/core/web";
import { decodeGenXFrequency, encodeGenXFrequency } from "@freqgen/spooky2";
import { WCH_USB_VENDOR_ID } from "./byteport";

const REPLY_TIMEOUT_MS = 300;

export interface StoredProgram {
  slot: number;
  waveform: number;
  amplitudeV: number;
  dwellS: number;
  frequenciesHz: number[];
}

/** A program to store in a slot of the unit (as the PC software's upload does). */
export interface DemoProgram {
  name: string;
  /** Waveform code of the program header: 11 = sine. */
  waveform: number;
  amplitudeV: number;
  dwellS: number;
  frequenciesHz: number[];
}

export const DEMO_PROGRAM: DemoProgram = {
  name: "Demo program (browser)",
  waveform: 11,
  amplitudeV: 2,
  dwellS: 10,
  frequenciesHz: [100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200],
};

const pad2 = (n: number) => String(n).padStart(2, "0");
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

export class DisplayLink {
  private readonly t: WebSerialTransport

  private constructor(t: WebSerialTransport) {
    this.t = t
  }

  static async connect(): Promise<DisplayLink> {
    const t = await WebSerialTransport.request([{ usbVendorId: WCH_USB_VENDOR_ID }]);
    await t.open({ baudRate: 115200, dataBits: 8, stopBits: 1, parity: "none", flowControl: "none" });
    return new DisplayLink(t);
  }

  async close(): Promise<void> {
    await this.t.close();
  }

  /** Send one line; the first reply line, or null on timeout. */
  async ask(line: string, timeoutMs = REPLY_TIMEOUT_MS): Promise<string | null> {
    await this.t.flush();
    await this.t.write(`${line}\r\n`);
    try {
      return (await this.t.readLine(timeoutMs)).trim();
    } catch {
      return null;
    }
  }

  /** The revision the display reports, e.g. "Rev201". */
  async revision(): Promise<string | null> {
    const r = await this.ask(":n00=$");
    return r && r.startsWith(":Rev") ? r.slice(1) : null;
  }

  /** Round-trip times of the version query in ms, with a random 0–20 ms pause between requests. */
  async replyTimes(count: number, onProgress: (done: number) => void): Promise<number[]> {
    const times: number[] = [];
    for (let i = 0; i < count; i++) {
      await new Promise((r) => setTimeout(r, Math.random() * 20));
      await this.t.flush();
      const t0 = performance.now();
      await this.t.write(":n00=$\r\n");
      try {
        const line = await this.t.readLine(REPLY_TIMEOUT_MS);
        if (line.startsWith(":Rev")) times.push(performance.now() - t0);
      } catch {
        /* a timeout counts as no reply */
      }
      if (i % 10 === 9) onProgress(i + 1);
    }
    onProgress(count);
    return times;
  }

  /** The PC-mode title of this link (shown on the unit's PC screen); printable ASCII, max 59. */
  async showTitle(text: string): Promise<void> {
    const clean = text.replace(/[^\x20-\x7e]/g, "?").slice(0, 59);
    await this.ask(`:n00=${clean}`);
  }

  async backlight(on: boolean): Promise<string | null> {
    return this.ask(on ? ":w64=18888881," : ":w64=10000001,");
  }

  /** The name stored for a program slot (1–30), "" for none, or null when not answered. */
  async programName(slot: number): Promise<string | null> {
    const nn = String(slot).padStart(2, "0");
    const r = await this.ask(`:n${nn}=?`);
    if (!r || !r.startsWith(`:n${nn}=`)) return null;
    return r.slice(5);
  }

  /**
   * Store a program in a slot (1–30): its name, an empty step-parameter table (no gating), then
   * the program itself, which the unit writes to its memory. Each line must be answered ":ok".
   */
  async storeProgram(slot: number, p: DemoProgram): Promise<void> {
    const nn = pad2(slot);
    const expectOk = (reply: string | null, what: string) => {
      if (reply !== ":ok") throw new Error(`The unit did not accept the ${what} (reply ${reply ?? "none"}).`);
    };
    expectOk(await this.ask(`:n${nn}=${p.name.replace(/[^\x20-\x7e]/g, "?").slice(0, 59)}`, 1500), "program name");
    expectOk(await this.ask(`:g${nn}=${new Array(2 * p.frequenciesHz.length).fill(0).join(",")},`, 1500), "step table");
    const fields = [p.waveform, Math.round(p.amplitudeV * 100), 120, p.dwellS, p.frequenciesHz.length,
      ...p.frequenciesHz.map(encodeGenXFrequency)].join(",");
    expectOk(await this.ask(`:p${nn}=${fields},`, 2500), "program");
  }

  /** Erase the program in a slot (its name stays until it is overwritten). */
  async eraseProgram(slot: number): Promise<void> {
    const r = await this.ask(`:n${pad2(slot)}=,`, 2500);
    if (r !== ":ok") throw new Error(`The unit did not erase slot ${slot} (reply ${r ?? "none"}).`);
  }

  /**
   * Show a sweep on the PC screen, as the PC software drives it: a title, then a frequency line
   * every `stepMs`. The display mirrors each value; the page never switches an output on.
   */
  async pcSweep(opts: { title: string; fromHz: number; toHz: number; seconds: number; stepMs?: number },
                onFrequency: (hz: number) => void, stopped: () => boolean): Promise<void> {
    const stepMs = opts.stepMs ?? 150;
    await this.showTitle(opts.title);
    const steps = Math.max(2, Math.round((opts.seconds * 1000) / stepMs));
    for (let i = 0; i < steps && !stopped(); i++) {
      const hz = Math.round((opts.fromHz + ((opts.toHz - opts.fromHz) * i) / (steps - 1)) * 100) / 100;
      await this.t.flush();
      await this.t.write(`:w24=${encodeGenXFrequency(hz)},\r\n`);
      onFrequency(hz);
      await sleep(stepMs);
    }
  }

  /** Clear the PC screen's title and frequencies for this channel. */
  async clearPcScreen(): Promise<void> {
    await this.ask(":w97=12621,");
  }

  async program(slot: number): Promise<StoredProgram | null> {
    const nn = String(slot).padStart(2, "0");
    const r = await this.ask(`:n${nn}=*`);
    const m = r ? /^:p\d\d=(.*)$/.exec(r) : null;
    if (!m) return null;
    const v = m[1]!.split(",").filter((s) => s !== "").map(Number);
    if (v.length < 5) return null;
    const [waveform, amplitude, , dwell, count] = v as [number, number, number, number, number];
    return {
      slot,
      waveform,
      amplitudeV: amplitude / 100,
      dwellS: dwell,
      frequenciesHz: v.slice(5, 5 + count).map(decodeGenXFrequency),
    };
  }
}

export function percentile(sorted: number[], p: number): number {
  if (!sorted.length) return NaN;
  const x = ((sorted.length - 1) * p) / 100;
  const lo = Math.floor(x);
  const hi = Math.min(lo + 1, sorted.length - 1);
  return sorted[lo]! + (sorted[hi]! - sorted[lo]!) * (x - lo);
}
