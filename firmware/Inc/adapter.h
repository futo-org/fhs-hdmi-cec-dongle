#ifndef ADAPTER_H
#define ADAPTER_H

#include <stdint.h>

#include "tusb.h"

/* The CEC adapter the kernel driver talks to: vendor control requests in,
   one event per interrupt IN transfer out. driver/fhs-cec-proto.h is the
   contract. Idle until the host enables it, and idle again on USB reset. */

void adapter_init(void);

/* Move CEC results and frames to the host. Main loop. */
void adapter_task(void);

/* USB bus reset or detach. */
void adapter_reset(void);

/* Vendor control requests addressed to the CEC interface. Returns false to
   stall. */
bool adapter_control(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);

int adapter_enabled(void);

/* Bench: send payload (opcode + operands, empty = poll) to dest, from the
   lowest logical address the host claimed, else from Unregistered (15).
   The result goes to the console, not the host. */
int adapter_send_local(uint8_t dest, const uint8_t *payload, uint8_t len);

/* Bench: break the next matching exchange on purpose, to exercise the
   driver's error paths against the real kernel CEC core. One shot each. */
typedef enum {
    ADAPTER_FAULT_NONE,
    ADAPTER_FAULT_NACK,         /* next host frame reports NACK, unsent */
    ADAPTER_FAULT_LATE_TX_DONE, /* next host result arrives past the kernel timeout */
    ADAPTER_FAULT_DROP_RX,      /* next received frame never reaches the host */
    ADAPTER_FAULT_WEDGE_TX,     /* next host result is never reported */
} adapter_fault_t;

void adapter_inject_fault(adapter_fault_t fault);
adapter_fault_t adapter_pending_fault(void);

#endif
