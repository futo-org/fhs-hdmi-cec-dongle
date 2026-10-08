#ifndef CEC_LOG_H
#define CEC_LOG_H

#include "cec.h"

/* Bench console: every frame, both directions, raw bytes then meaning.
   Compiled out of release builds. */

#if FHS_CEC_DEBUG_SHELL

void cec_log_rx(const cec_frame_t *f);

/* tag: who sent it, when not the host ("local", "fault"), or NULL */
void cec_log_tx(const uint8_t *frame, uint8_t len, cec_tx_result_t r, const char *tag);

/* Print frames for other devices too. Off by default. */
void cec_log_set_show_all(int on);
int cec_log_get_show_all(void);

/* Raw wire bytes on a line before each decoded frame. On by default. */
void cec_log_set_show_raw(int on);
int cec_log_get_show_raw(void);

#else

static inline void cec_log_rx(const cec_frame_t *f)
{
    (void)f;
}

static inline void cec_log_tx(const uint8_t *frame, uint8_t len, cec_tx_result_t r,
                              const char *tag)
{
    (void)frame;
    (void)len;
    (void)r;
    (void)tag;
}

#endif

#endif
