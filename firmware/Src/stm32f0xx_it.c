/* Vector table entries. Startup declares these weak. */

#include "cec.h"
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

/* Shared with CAN, which this board does not use. */
void CEC_CAN_IRQHandler(void)
{
    cec_irq_handler();
}
