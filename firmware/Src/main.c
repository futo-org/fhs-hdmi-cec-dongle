/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : FHS CEC dongle: wiring only. Work lives in the modules.
 ******************************************************************************
 */

#include "main.h"
#include "adapter.h"
#include "cec.h"
#include "led.h"
#include "system_clock.h"
#include "usb.h"

#if FHS_CEC_DEBUG_SHELL
#include "shell.h"
#endif

int main(void)
{
    HAL_Init();
    system_clock_config();
    led_init();
    cec_init();
    adapter_init();
    usb_init();

    for (;;) {
        usb_task();
        cec_task();
        adapter_task();
        led_task();
#if FHS_CEC_DEBUG_SHELL
        shell_task();
#endif
    }
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;) {
    }
}
