/* Independent watchdog (nx_wdg.h). Device only. */
#include "nx_wdg.h"

#include "nx_config.h"
#include "stm32g0xx.h"

#define KEY_START 0xCCCCu
#define KEY_ACCESS 0x5555u
#define KEY_RELOAD 0xAAAAu

void nx_wdg_start(void)
{
    if (!NX_IWDG_ENABLE)
        return;
    IWDG->KR = KEY_START;          /* also switches LSI on */
    IWDG->KR = KEY_ACCESS;
    IWDG->PR = NX_IWDG_PR;
    IWDG->RLR = NX_IWDG_RLR;
    for (unsigned i = 0; i < 100000u && IWDG->SR; i++) {   /* PR/RLR update, a few LSI cycles */
    }
    IWDG->KR = KEY_RELOAD;
}

void nx_wdg_feed(void)
{
    IWDG->KR = KEY_RELOAD;   /* ignored while the watchdog is not running */
}
