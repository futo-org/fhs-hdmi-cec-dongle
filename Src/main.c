/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Nucleo-F072RB: wiring only. Work lives in the modules.
 ******************************************************************************
 */

#include "main.h"
#include "cec.h"
#include "console.h"
#include "led.h"
#include "shell.h"
#include "system_clock.h"
#include "uart.h"

int main(void)
{
    HAL_Init();
    system_clock_config();
    led_init();
    uart_init();
    cec_init();
    shell_begin();

    for (;;) {
        const char *line = console_take_line();

        if (line != NULL) {
            shell_execute(line);
        }
        led_task();
        cec_task();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;) {
    }
}
