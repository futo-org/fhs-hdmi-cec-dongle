#ifndef CEC_H
#define CEC_H

#include <stddef.h>
#include <stdint.h>

/* Header + opcode + up to 14 operands. */
#define CEC_FRAME_MAX       16U

#define CEC_ADDR_BROADCAST  0x0FU
#define CEC_ADDR_COUNT      16U

/* Longest legal frame is 16 bytes at ~24 ms each, plus signal free time and
   the hardware's own arbitration retries. Past this a transmit is wedged. */
#define CEC_TX_TIMEOUT_MS   1000U

typedef struct {
    uint32_t ms;
    uint8_t len;
    uint8_t data[CEC_FRAME_MAX];    /* header first */
} cec_frame_t;

typedef enum {
    CEC_TX_OK,
    CEC_TX_NACK,
    CEC_TX_ARB_LOST,
    CEC_TX_LOW_DRIVE,
    CEC_TX_ERROR,
} cec_tx_result_t;

typedef struct {
    uint32_t rx_frames;
    uint32_t rx_errors;
    uint32_t rx_dropped;
    uint32_t tx_frames;
    uint32_t tx_nack;
    uint32_t tx_arb_lost;
    uint32_t tx_low_drive;
    uint32_t tx_error;
    uint32_t recoveries;
} cec_stats_t;

void cec_init(void);

/* Called by the vector in stm32f0xx_it.c. Shared with CAN. */
void cec_irq_handler(void);

/* Timeouts and receiver watchdogs. Main loop. */
void cec_task(void);

/* Bit n set = acknowledge frames addressed to logical address n. 15 is
   broadcast and never acknowledged. Aborts a frame in flight, which then
   reports CEC_TX_ERROR. */
void cec_set_ack_mask(uint16_t mask);
uint16_t cec_get_ack_mask(void);

/* Send one frame, header first. A transmit still in flight is abandoned
   without a result. Every accepted frame produces exactly one result within
   CEC_TX_TIMEOUT_MS. Returns 0 on a bad length. */
int cec_transmit(const uint8_t *frame, uint8_t len);

/* Every frame on the bus, ours to filter. Returns 0 when empty. */
int cec_take_rx(cec_frame_t *out);

/* Result of the last cec_transmit. Returns 0 while there is none. */
int cec_take_tx_result(cec_tx_result_t *out);

void cec_get_stats(cec_stats_t *out);

/* Re-init the peripheral, keeping the ack mask and tolerance. A frame in
   flight reports CEC_TX_ERROR. */
void cec_recover(void);

/* Bench use, debug shell only. */
const char *cec_state_name(void);
void cec_dump(void);
void cec_set_extended_tolerance(int on);
int cec_get_extended_tolerance(void);

/* Hand the pin to plain GPIO so cec_diag can measure the line directly, and
   give it back. Paired; the peripheral is disabled in between. */
void cec_bus_suspend(void);
void cec_bus_resume(void);

#endif
