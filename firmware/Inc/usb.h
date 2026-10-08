#ifndef USB_H
#define USB_H

#include <stdint.h>

void usb_init(void);

/* TinyUSB device task. Main loop. */
void usb_task(void);

/* Called by the vector in stm32f0xx_it.c. */
void usb_irq_handler(void);

/* One event on the interrupt IN endpoint. Returns 0 while the previous one
   is still in flight or the host has not configured us. */
int usb_event_send(const uint8_t *data, uint8_t len);

/* Debug console over CDC-ACM. Next received byte, -1 if none. */
int usb_console_getc(void);

#endif
