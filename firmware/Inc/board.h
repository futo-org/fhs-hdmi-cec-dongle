/* FHS CEC dongle (hardware/pcb), STM32F072CBU6. Everything board-specific lives here. */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f0xx_hal.h"

/* Status LED, active high */
#define BOARD_LED_PORT             GPIOA
#define BOARD_LED_PIN              GPIO_PIN_5
#define BOARD_LED_CLK_ENABLE()     __HAL_RCC_GPIOA_CLK_ENABLE()

/* USB FS on PA11/PA12, USB-C. No crystal: the USB clock is HSI48 trimmed by CRS
   against the host's start-of-frame. */
#define BOARD_USB_IRQn             USB_IRQn
#define BOARD_USB_IRQ_PRIORITY     2U

/* HDMI-CEC on PB8/AF0 through 100R. Open drain, no internal pull. */
#define BOARD_CEC                   CEC
#define BOARD_CEC_PORT              GPIOB
#define BOARD_CEC_PIN               GPIO_PIN_8
#define BOARD_CEC_AF                GPIO_AF0_CEC
#define BOARD_CEC_IRQn              CEC_CAN_IRQn   /* NVIC 30, shared with CAN */
#define BOARD_CEC_IRQ_PRIORITY      3U             /* M0 has 0..3 */
#define BOARD_CEC_CLK_ENABLE()      __HAL_RCC_CEC_CLK_ENABLE()
#define BOARD_CEC_PORT_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()

/* The 27k bus pull-up hangs off PB9 rather than 3V3, so an unpowered dongle
   does not load the HDMI bus. Driven high while we run. */
#define BOARD_CEC_PULLUP_PORT       GPIOB
#define BOARD_CEC_PULLUP_PIN        GPIO_PIN_9

#endif
