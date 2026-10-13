/* OpenGX Pro classic firmware: reimplementation of the original v7789 behaviour.
 * Boot order follows the original main 0x0800E404 (docs/spec/application.md §2). */
#include "app.h"
#include "board.h"
#include "lcd.h"
#include "link.h"
#include "spiflash.h"
#include "storage.h"
#include "uart.h"

int main(void)
{
    clock_init();
    board_init();
    uart_init();
    link_init();
    spiflash_init();
    storage_init();
    __enable_irq();

    delay_loop(0x82355);
    lcd_init();
    app_run();
}
