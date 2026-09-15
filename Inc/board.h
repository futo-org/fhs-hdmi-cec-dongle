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

/* HDMI-CEC on PB8/AF0 (Arduino D15). HDMI pin 13, ground ref pin 17.
   Open drain, no internal pull: needs an external 27k to 3V3, the Nucleo has none. */
#define BOARD_CEC                   CEC
#define BOARD_CEC_PORT              GPIOB
#define BOARD_CEC_PIN               GPIO_PIN_8
#define BOARD_CEC_AF                GPIO_AF0_CEC
#define BOARD_CEC_IRQn              CEC_CAN_IRQn   /* NVIC 30, shared with CAN */
#define BOARD_CEC_IRQ_PRIORITY      3U             /* M0 has 0..3; below the console */
#define BOARD_CEC_CLK_ENABLE()      __HAL_RCC_CEC_CLK_ENABLE()
#define BOARD_CEC_PORT_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()

#endif
