/* TinyUSB build configuration, read by Drivers/tinyusb. */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_MCU             OPT_MCU_STM32F0
#define CFG_TUSB_OS              OPT_OS_NONE
#define CFG_TUSB_DEBUG           0

#define CFG_TUD_ENABLED          1
#define CFG_TUD_MAX_SPEED        OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE   64

/* The bench console. Release builds leave the CDC interface out of the
   descriptors, but the class stays compiled: TinyUSB 0.21 cannot build with
   no built-in class at all (zero-size driver table in usbd.c). */
#define CFG_TUD_CDC              1
#define CFG_TUD_CDC_EP_BUFSIZE   64
#define CFG_TUD_CDC_RX_BUFSIZE   64
#if FHS_CEC_DEBUG_SHELL
#define CFG_TUD_CDC_TX_BUFSIZE   1024    /* a `cec dump` fits without stalling */
#else
#define CFG_TUD_CDC_TX_BUFSIZE   64
#endif

#endif
