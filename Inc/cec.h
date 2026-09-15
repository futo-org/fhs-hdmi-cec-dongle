#ifndef CEC_H
#define CEC_H

#include <stddef.h>
#include <stdint.h>

/* Header + opcode + up to 14 operands. */
#define CEC_FRAME_MAX       16U

#define CEC_ADDR_BROADCAST  0x0FU
#define CEC_ADDR_NONE       0xFFU   /* no own address: acknowledge nothing */

void cec_init(void);

/* Called by the vector in stm32f0xx_it.c. Shared with CAN. */
void cec_irq_handler(void);

/* Prints received frames and transmit results. Main loop, never the ISR. */
void cec_task(void);

const char *cec_state_name(void);

/* Registers, transmit/receive state and the interrupt path counters. */
void cec_dump(void);
uint32_t cec_rx_count(void);
uint32_t cec_tx_count(void);
uint32_t cec_err_count(void);

uint8_t cec_get_address(void);

/* 0..15, or CEC_ADDR_NONE. Returns 0 if rejected. Cancels allocation. */
int cec_set_address(uint8_t logical);

/* Claim the first free Playback address (4, 8, 11) by polling. Runs at boot
   on its own; this restarts it. Result is printed from cec_task. */
void cec_allocate_address(void);
int cec_allocating(void);

/* Re-init the peripheral, keeping address and tolerance. The only way out of
   a transmit wedged by lost arbitration. */
void cec_recover(void);

/* Extended Rx tolerance widens the accepted bit windows. Returns 0 if the
   re-init failed. */
int cec_set_extended_tolerance(int on);
int cec_get_extended_tolerance(void);

/* Hardware listening mode. Full (the default) receives frames addressed to
   anyone, without acknowledging them; reduced lets the hardware drop them,
   and on this silicon that path wedges the receiver, see cec.c. */
int cec_set_listen_all(int on);
int cec_get_listen_all(void);

/* Console filter: print frames for other devices too. Off by default. */
void cec_set_show_all(int on);
int cec_get_show_all(void);

/* Console: raw wire bytes (header included) on a line before each decoded
   frame. On by default. */
void cec_set_show_raw(int on);
int cec_get_show_raw(void);

/* Answer the standard interrogation opcodes automatically. On by default;
   a device the TV cannot identify does not get to control it. */
void cec_set_auto_respond(int on);
int cec_get_auto_respond(void);

/* Hand the pin to plain GPIO so cec_diag can measure the line directly, and
   give it back. Paired; the peripheral is disabled in between. */
void cec_bus_suspend(void);
void cec_bus_resume(void);

/* Queue one frame. payload is opcode + operands, NULL/0 sends a bare header
   (a poll). Returns 0 if the peripheral is busy or the arguments are bad. */
int cec_send(uint8_t dest, const uint8_t *payload, size_t len);

/* One Touch Play: wake the TV and switch it to our port. <Active Source>
   follows once <Image View On> is acknowledged. Returns 0 if busy. */
int cec_one_touch_play(void);

/* <Standby> to the TV. */
int cec_standby(void);

/* Whether the TV is showing our port, as far as the bus has told us. */
int cec_is_active_source(void);

/* <Give Device Power Status> to the TV; the answer prints as "tv is on",
   "standby", "turning on" or "turning off". Returns 0 if busy. */
int cec_tv_query_power(void);

/* Volume keys, n presses each with its release. They go to the Audio System
   while it has System Audio Mode on, otherwise to the TV. Returns 0 if a
   key sequence is already running or there is no own address. */
int cec_volume_up(uint8_t n);
int cec_volume_down(uint8_t n);
int cec_volume_mute(void);

/* <Give Audio Status> to whoever has the volume; prints "audio: volume N".
   Only an Audio System answers it. */
int cec_audio_status(void);
int cec_system_audio_mode(void);

#endif
