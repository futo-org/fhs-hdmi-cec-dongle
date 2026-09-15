/* NUCLEO-F072RB pin map. Everything board-specific lives here. */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f0xx_hal.h"

/* User LED LD2 */
#define BOARD_LED_PORT             GPIOA
#define BOARD_LED_PIN              GPIO_PIN_5
#define BOARD_LED_CLK_ENABLE()     __HAL_RCC_GPIOA_CLK_ENABLE()

/* USART2 -> ST-LINK virtual COM port (/dev/ttyACM0) */
#define BOARD_CONSOLE_UART          USART2
#define BOARD_CONSOLE_IRQn          USART2_IRQn
#define BOARD_CONSOLE_BAUD          115200U
#define BOARD_CONSOLE_PORT          GPIOA
#define BOARD_CONSOLE_PINS          (GPIO_PIN_2 | GPIO_PIN_3)  /* PA2 TX, PA3 RX */
#define BOARD_CONSOLE_AF            GPIO_AF1_USART2
#define BOARD_CONSOLE_CLK_ENABLE()  __HAL_RCC_USART2_CLK_ENABLE()
#define BOARD_CONSOLE_PORT_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()

#endif
