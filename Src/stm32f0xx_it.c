/* Vector table entries. Startup declares these weak. */

#include "main.h"
#include "uart.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void USART2_IRQHandler(void)
{
    uart_irq_handler();
}
