#include "cec.h"
#include "board.h"
#include "main.h"

#include <string.h>

#if FHS_CEC_DEBUG_SHELL
#include <stdio.h>
#endif

#define RX_QUEUE       8U       /* power of two */

/* Bytes of one frame land ~24 ms apart. A byte arriving long after the last
   one cannot belong to the same frame: whatever was there is remains of a
   frame the hardware aborted without telling us. */
#define RX_BYTE_GAP_MAX_MS 60U

/* A stuck receiver overruns repeatedly AND delivers nothing. Overruns on
   their own are normal when the TV fires queries back to back, and re-initing
   then would abort a frame in flight and cause the next overrun: the cure
   becomes the disease. Both conditions must hold. */
#define RX_OVERRUN_STREAK_MAX 4U
#define RX_STUCK_SILENCE_MS   2000U

/* Directed opcodes whose reply is mandatory within 1 s (CEC 1.4 §7.4 and
   table 8). If one of these is acknowledged and nothing at all arrives
   afterwards, the receiver is the suspect, not the TV. */
static const uint8_t query_opcodes[] = {
    0x83,   /* Give Physical Address    -> 84 */
    0x8C,   /* Give Device Vendor ID    -> 87 */
    0x8F,   /* Give Device Power Status -> 90 */
    0x46,   /* Give OSD Name            -> 47 */
    0x9F,   /* Get CEC Version          -> 9E */
    0x91,   /* Get Menu Language        -> 32 */
};

#define REPLY_TIMEOUT_MS 1500U

#define CEC_RX_ERRORS (CEC_FLAG_RXOVR | CEC_FLAG_BRE | CEC_FLAG_SBPE | \
                       CEC_FLAG_LBPE | CEC_FLAG_RXACKE)
#define CEC_TX_ERRORS (CEC_FLAG_TXUDR | CEC_FLAG_TXERR | CEC_FLAG_TXACKE)

/* Every status flag, for clearing them in one go. */
#define CEC_FLAG_ALL (CEC_FLAG_RXBR | CEC_FLAG_RXEND | CEC_RX_ERRORS | \
                      CEC_FLAG_ARBLST | CEC_FLAG_TXBR | CEC_FLAG_TXEND | \
                      CEC_TX_ERRORS)

/* OAR has one bit per logical address 0..14. */
#define OAR_MASK 0x7FFFU

/* HAL is used for configuration only (clock, CFGR, IER). The interrupt path
   and the transmit/receive state are handled here: the HAL's IRQ handler
   clears flags with `ISR |= bit`, a read-modify-write that also acknowledges
   any flag raised between the read and the write. A byte that lands during a
   transmit interrupt gets its RXBR wiped with RXDR unread, and the receiver
   then overruns on every byte after it. Plain `ISR = bit` stores never do
   that. */
static CEC_HandleTypeDef cec;

/* Frame being received. Bytes land here from the ISR one at a time. */
static uint8_t rx_buf[CEC_FRAME_MAX];
static volatile uint8_t rx_len;
static uint32_t rx_last_byte_ms;

/* SPSC: ISR owns head, main owns tail. */
static cec_frame_t rx_queue[RX_QUEUE];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;

/* Frame being transmitted, header included. */
static uint8_t tx_buf[CEC_FRAME_MAX];
static volatile uint8_t tx_len;
static volatile uint8_t tx_next;
static volatile uint8_t tx_busy;        /* TXSOM issued, no end seen yet */
static volatile uint8_t tx_wants_reply; /* directed query: an answer is due */
static volatile uint8_t tx_arb_lost;    /* lost arbitration at least once */
static volatile uint8_t tx_complete;
static volatile uint32_t tx_error;
static uint8_t tx_pending;              /* main-loop view: a result is owed */
static uint32_t tx_started_ms;

static uint8_t result_ready;
static cec_tx_result_t result;

static volatile uint8_t reply_wait;     /* acked query, nothing back yet */
static volatile uint32_t reply_deadline_ms;
static volatile uint32_t rx_overrun_streak;
static volatile uint32_t rx_last_frame_ms;
static volatile uint32_t rx_last_overrun_ms;

static volatile cec_stats_t stats;

static uint16_t ack_mask;

#if FHS_CEC_DEBUG_SHELL
#define OVR_LOG 4U              /* last few overruns, with context */
#define EV_LOG  32U             /* ISR passes, newest last; power of two */

typedef struct {
    uint32_t ms;
    uint16_t isr;               /* ISR snapshot the overrun came in with */
    uint8_t cr;                 /* CECEN/TXSOM/TXEOM at that moment */
    uint8_t tx_busy;
    uint8_t rx_len;             /* bytes of the frame it killed */
    uint8_t rxdr;               /* what the data register held */
} ovr_t;

typedef struct {
    uint32_t ms;
    uint16_t isr;
    uint8_t byte;               /* RXDR on a byte or overrun event */
    uint8_t rx_len;
} ev_t;

static volatile uint32_t stat_rxbr_during_tx;
static volatile uint32_t stat_late_flags;
static volatile uint32_t stat_rx_resync;
static volatile uint32_t stat_rx_bad_len;
static volatile uint32_t stat_watchdog;
static volatile uint32_t stat_foreign_nack;
static ovr_t ovr_log[OVR_LOG];
static volatile uint8_t ovr_count;
static ev_t ev_log[EV_LOG];
static volatile uint32_t ev_count;
#define DEBUG_COUNT(counter) ((counter)++)
#else
#define DEBUG_COUNT(counter) ((void)0)
#endif

static void cec_msp_init(void);
static void cec_pin_to_af(void);
static void cec_rearm(void);
static void tx_abandon(void);

void cec_init(void)
{
    cec.Instance = BOARD_CEC;

    /* SFT by hardware: it applies the spec's 3/5/7 bit periods itself. */
    cec.Init.SignalFreeTime = CEC_DEFAULT_SFT;
    cec.Init.Tolerance = CEC_STANDARD_TOLERANCE;

    /* Every receive error aborts the frame, so the byte count can be reset on
       any of them. Without this a bit rising error lets reception continue
       and the frame arrives with a hole in it. */
    cec.Init.BRERxStop = CEC_RX_STOP_ON_BRE;

    cec.Init.BREErrorBitGen = CEC_BRE_ERRORBIT_NO_GENERATION;
    cec.Init.LBPEErrorBitGen = CEC_LBPE_ERRORBIT_NO_GENERATION;
    cec.Init.BroadcastMsgNoErrorBitGen = CEC_BROADCASTERROR_NO_ERRORBIT_GENERATION;

    cec.Init.SignalFreeTimeOption = CEC_SFT_START_ON_TXSOM;

    /* Full listening: every frame on the bus is received and the address
       filter is applied in software. With a second device on the bus (the
       TV's soundbar) reduced listening went deaf within seconds of every TV
       power transition: no flag, no error, nothing delivered until a
       re-init. Receiving them all avoids that path entirely. */
    cec.Init.ListenMode = CEC_FULL_LISTENING_MODE;
    cec.Init.OwnAddress = CEC_OWN_ADDRESS_NONE;
    cec.Init.RxBuffer = rx_buf;     /* unused: the HAL IRQ handler is not */

    cec_msp_init();
    if (HAL_CEC_Init(&cec) != HAL_OK) {
        Error_Handler();
    }
    cec_rearm();
}

uint16_t cec_get_ack_mask(void)
{
    return ack_mask;
}

void cec_set_ack_mask(uint16_t mask)
{
    CEC_TypeDef *r = BOARD_CEC;

    mask &= OAR_MASK;

    /* HAL_CEC_SetDeviceAddress ORs the new address in without clearing the
       old one, so a changed address would leave both acknowledged. CFGR is
       writable only with CECEN clear, which aborts any frame in flight. */
    __HAL_CEC_DISABLE(&cec);
    r->CFGR = (r->CFGR & ~CEC_CFGR_OAR) | ((uint32_t)mask << CEC_CFGR_OAR_Pos);
    cec.Init.OwnAddress = mask;     /* a later re-init must keep it */
    ack_mask = mask;
    tx_abandon();
    cec_rearm();
    __HAL_CEC_ENABLE(&cec);
}

/* HAL_CEC_Init disables the peripheral and rewrites CFGR and IER from Init.
   MspInit is skipped once gState left RESET. */
static void reinit(void)
{
    if (HAL_CEC_Init(&cec) != HAL_OK) {
        Error_Handler();
    }
    tx_abandon();
    cec_rearm();
    rx_overrun_streak = 0;
}

void cec_recover(void)
{
    stats.recoveries++;
    reinit();
}

int cec_get_extended_tolerance(void)
{
    return cec.Init.Tolerance == CEC_EXTENDED_TOLERANCE;
}

void cec_set_extended_tolerance(int on)
{
    cec.Init.Tolerance = on ? CEC_EXTENDED_TOLERANCE : CEC_STANDARD_TOLERANCE;
    reinit();
}

static int is_query(const uint8_t *frame, uint8_t len)
{
    if (len < 2U || (frame[0] & 0x0FU) == CEC_ADDR_BROADCAST) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(query_opcodes); i++) {
        if (frame[1] == query_opcodes[i]) {
            return 1;
        }
    }
    return 0;
}

int cec_transmit(const uint8_t *frame, uint8_t len)
{
    CEC_TypeDef *r = BOARD_CEC;

    if (len == 0U || len > CEC_FRAME_MAX) {
        return 0;
    }

    /* The host only sends again once it has a result, or after giving up on
       one. Either way the old frame is no longer anyone's business. */
    if (tx_pending) {
        tx_pending = 0;             /* abandoned silently, see cec.h */
        reinit();
    }
    result_ready = 0;

    memcpy(tx_buf, frame, len);
    tx_len = len;
    tx_next = 1;
    tx_complete = 0;
    tx_error = 0;
    tx_arb_lost = 0;
    tx_wants_reply = (uint8_t)is_query(frame, len);
    tx_pending = 1;
    tx_busy = 1;
    tx_started_ms = HAL_GetTick();

    /* RM0091 §31.7.1: TXEOM before TXDR, TXDR before TXSOM. A header-only
       frame (a poll) is its own last byte. */
    if (len == 1U) {
        r->CR |= CEC_CR_TXEOM;
    }
    r->TXDR = tx_buf[0];
    r->CR |= CEC_CR_TXSOM;
    return 1;
}

int cec_take_rx(cec_frame_t *out)
{
    if (rx_tail == rx_head) {
        return 0;
    }
    *out = rx_queue[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1U) % RX_QUEUE);
    return 1;
}

int cec_take_tx_result(cec_tx_result_t *out)
{
    if (!result_ready) {
        return 0;
    }
    result_ready = 0;
    *out = result;
    return 1;
}

void cec_get_stats(cec_stats_t *out)
{
    __disable_irq();
    memcpy(out, (const void *)&stats, sizeof(*out));
    __enable_irq();
}

static void finish_tx(cec_tx_result_t r)
{
    tx_pending = 0;
    result = r;
    result_ready = 1;

    switch (r) {
    case CEC_TX_OK:        stats.tx_frames++;    break;
    case CEC_TX_NACK:      stats.tx_nack++;      break;
    case CEC_TX_ARB_LOST:  stats.tx_arb_lost++;  break;
    case CEC_TX_LOW_DRIVE: stats.tx_low_drive++; break;
    case CEC_TX_ERROR:     stats.tx_error++;     break;
    }
}

/* Directed: no ack. Broadcast: the ack slot is inverted and a low means a
   follower rejected it; Linux reports both as NACK. */
static cec_tx_result_t tx_error_result(uint32_t bits)
{
    if ((bits & CEC_FLAG_TXUDR) != 0U) {
        return CEC_TX_ERROR;
    }
    if ((bits & CEC_FLAG_TXERR) != 0U) {
        return CEC_TX_LOW_DRIVE;
    }
    return CEC_TX_NACK;
}

/* A re-init or address change aborted the frame in flight. The host is
   still owed a result, and ERROR is the one it retries. */
static void tx_abandon(void)
{
    if (tx_pending) {
        finish_tx(CEC_TX_ERROR);
    }
}

void cec_task(void)
{
    uint32_t bits;

    if (tx_pending && tx_complete) {
        tx_complete = 0;
        finish_tx(CEC_TX_OK);
    }

    bits = tx_error;
    if (tx_pending && bits != 0U) {
        tx_error = 0;
        finish_tx(tx_error_result(bits));
    }

    /* Arbitration lost is not a failure: the hardware keeps TXSOM pending and
       retries after the winner's frame. But a bus that never goes quiet, or a
       transmitter wedged some other way, would leave us busy forever and keep
       the line jammed. */
    if (tx_pending && (HAL_GetTick() - tx_started_ms) > CEC_TX_TIMEOUT_MS) {
        cec_tx_result_t r = tx_arb_lost ? CEC_TX_ARB_LOST : CEC_TX_ERROR;

        tx_pending = 0;             /* recover must not report it as ERROR */
        cec_recover();
        finish_tx(r);
    }

    /* The other side acknowledged a question it must answer and the bus has
       been silent since. Only a deaf receiver produces that; a re-init is the
       known cure and is harmless if the follower was merely rude. */
    if (reply_wait && (HAL_GetTick() - reply_deadline_ms) < 0x80000000UL) {
        reply_wait = 0;
        DEBUG_COUNT(stat_watchdog);
        cec_recover();
    }

    /* Let a stale streak decay. A quiet bus (the TV asleep) would otherwise
       satisfy the silence test forever and re-init on an old overrun count. */
    if (rx_overrun_streak > 0U &&
        (HAL_GetTick() - rx_last_overrun_ms) > RX_STUCK_SILENCE_MS) {
        rx_overrun_streak = 0;
    }

    if (rx_overrun_streak >= RX_OVERRUN_STREAK_MAX &&
        (HAL_GetTick() - rx_last_frame_ms) > RX_STUCK_SILENCE_MS) {
        cec_recover();
    }
}

#if FHS_CEC_DEBUG_SHELL
const char *cec_state_name(void)
{
    int rx = rx_len > 0U;

    if (tx_busy && rx) {
        return "rx+tx";
    }
    if (tx_busy) {
        return "tx";
    }
    if (rx) {
        return "rx";
    }
    return (BOARD_CEC->CR & CEC_CR_CECEN) ? "ready" : "off";
}

void cec_dump(void)
{
    CEC_TypeDef *r = BOARD_CEC;
    uint8_t n = ovr_count;

    printf("CR %08lx CFGR %08lx IER %08lx ISR %08lx\n",
           (unsigned long)r->CR, (unsigned long)r->CFGR,
           (unsigned long)r->IER, (unsigned long)r->ISR);
    printf("tx busy %u pending %u len %u next %u | rx len %u q %u/%u\n",
           tx_busy, tx_pending, tx_len, tx_next, rx_len, rx_head, rx_tail);
    printf("rx %lu err %lu drop %lu | tx ok %lu nack %lu arb %lu low %lu err %lu\n",
           (unsigned long)stats.rx_frames, (unsigned long)stats.rx_errors,
           (unsigned long)stats.rx_dropped, (unsigned long)stats.tx_frames,
           (unsigned long)stats.tx_nack, (unsigned long)stats.tx_arb_lost,
           (unsigned long)stats.tx_low_drive, (unsigned long)stats.tx_error);
    printf("recoveries %lu (watchdog %lu), foreign nack %lu\n",
           (unsigned long)stats.recoveries, (unsigned long)stat_watchdog,
           (unsigned long)stat_foreign_nack);
    printf("rxbr during tx %lu, late flags %lu, resync %lu, bad len %lu\n",
           (unsigned long)stat_rxbr_during_tx, (unsigned long)stat_late_flags,
           (unsigned long)stat_rx_resync, (unsigned long)stat_rx_bad_len);
    printf("overruns %u", n);
    if (n > OVR_LOG) {
        n = OVR_LOG;
    }
    printf("%s\n", n ? ", last:" : "");
    for (uint8_t i = 0; i < n; i++) {
        const ovr_t *o = &ovr_log[i];

        printf("  [%8lu] isr %04x cr %02x tx_busy %u rx_len %u rxdr %02x\n",
               (unsigned long)o->ms, o->isr, o->cr, o->tx_busy, o->rx_len,
               o->rxdr);
    }

    printf("isr passes %lu, last:\n", (unsigned long)ev_count);
    {
        uint32_t total = ev_count;
        uint32_t first = total > EV_LOG ? total - EV_LOG : 0U;

        for (uint32_t i = first; i < total; i++) {
            const ev_t *e = &ev_log[i % EV_LOG];

            printf("  [%8lu] isr %04x byte %02x rx_len %u\n",
                   (unsigned long)e->ms, e->isr, e->byte, e->rx_len);
        }
    }
}
#endif

/* --- interrupt path, must not block --- */

static void rx_deliver(void)
{
    uint8_t next;
    uint8_t size = rx_len;

    rx_overrun_streak = 0;          /* a whole frame arrived: not stuck */
    rx_last_frame_ms = HAL_GetTick();
    reply_wait = 0;                 /* anything at all proves we can hear */

    if (size == 0U || size > CEC_FRAME_MAX) {
        DEBUG_COUNT(stat_rx_bad_len);
        return;
    }
    stats.rx_frames++;

    next = (uint8_t)((rx_head + 1U) % RX_QUEUE);
    if (next == rx_tail) {          /* full: drop, never overwrite */
        stats.rx_dropped++;
        return;
    }

    rx_queue[rx_head].ms = HAL_GetTick();
    rx_queue[rx_head].len = size;
    memcpy(rx_queue[rx_head].data, rx_buf, size);
    rx_head = next;
}

static void rx_fail(uint32_t bits, uint8_t rxdr)
{
    /* Someone else's frame that nobody acknowledged, typically the TV
       polling its soundbar while that is in standby. Nothing of ours was
       lost, so it is counted rather than reported. In full listening mode
       it arrives as RXACKE, documented. With the hardware address filter on
       it arrived as RXOVR with no byte delivered, which is not documented
       anywhere. */
    if (bits == CEC_FLAG_RXACKE || bits == CEC_FLAG_RXOVR) {
        uint8_t dest = rx_buf[0] & 0x0FU;

        if (rx_len == 0U ||
            ((ack_mask & (1U << dest)) == 0U && dest != CEC_ADDR_BROADCAST)) {
            DEBUG_COUNT(stat_foreign_nack);
            rx_len = 0;
            return;
        }
    }

    stats.rx_errors++;

    if ((bits & CEC_FLAG_RXOVR) != 0U) {
#if FHS_CEC_DEBUG_SHELL
        ovr_t *o = &ovr_log[ovr_count % OVR_LOG];

        o->ms = HAL_GetTick();
        o->isr = (uint16_t)bits;
        o->cr = (uint8_t)BOARD_CEC->CR;
        o->tx_busy = tx_busy;
        o->rx_len = rx_len;
        o->rxdr = rxdr;
        ovr_count++;
#else
        (void)rxdr;
#endif
        rx_overrun_streak++;
        rx_last_overrun_ms = HAL_GetTick();
    } else {
        rx_overrun_streak = 0;
    }

    /* Every enabled receive error aborts the frame (BRE because BRESTP is
       set). The next byte on the bus starts a new one. */
    rx_len = 0;
}

void cec_irq_handler(void)
{
    CEC_TypeDef *r = BOARD_CEC;
    uint32_t isr;
#if FHS_CEC_DEBUG_SHELL
    int pass = 0;
#endif

    /* Rescan until nothing is pending. A flag raised while we were in here
       is served now instead of being lost, and every clear below is a plain
       store of the bits we have actually handled. */
    while ((isr = (r->ISR & r->IER)) != 0U) {
        uint8_t byte = 0;
#if FHS_CEC_DEBUG_SHELL
        ev_t *ev = &ev_log[ev_count % EV_LOG];

        if (pass++ > 0) {
            stat_late_flags++;
        }
        ev->ms = HAL_GetTick();
        ev->isr = (uint16_t)isr;
        ev->rx_len = rx_len;
#endif

        if ((isr & CEC_FLAG_RXBR) != 0U) {
            uint32_t now = HAL_GetTick();

            byte = (uint8_t)r->RXDR;

            r->ISR = CEC_FLAG_RXBR;

            if (rx_len > 0U && (now - rx_last_byte_ms) > RX_BYTE_GAP_MAX_MS) {
                DEBUG_COUNT(stat_rx_resync);
                rx_len = 0;
            }
            rx_last_byte_ms = now;

            if (rx_len < CEC_FRAME_MAX) {
                rx_buf[rx_len] = byte;
            }
            rx_len++;
            if (tx_busy) {
                DEBUG_COUNT(stat_rxbr_during_tx);
            }
        }

        if ((isr & CEC_FLAG_RXEND) != 0U) {
            r->ISR = CEC_FLAG_RXEND;
            rx_deliver();
            rx_len = 0;
        }

        if ((isr & CEC_RX_ERRORS) != 0U) {
            /* Read the data register even though the byte is lost: the
               overrun itself says the hardware still thinks it is unread. */
            uint8_t stale = (uint8_t)r->RXDR;

            r->ISR = isr & CEC_RX_ERRORS;
            if ((isr & CEC_FLAG_RXBR) == 0U) {
                byte = stale;
            }
            rx_fail(isr & CEC_RX_ERRORS, stale);
        }

        if ((isr & CEC_FLAG_ARBLST) != 0U) {
            /* Not an error: TXSOM stays pending and the hardware retries
               after the winner's frame. We just became its follower. */
            r->ISR = CEC_FLAG_ARBLST;
            tx_arb_lost = 1;
        }

        if ((isr & CEC_FLAG_TXBR) != 0U) {
            if (tx_next < tx_len) {
                if (tx_next + 1U == tx_len) {
                    r->CR |= CEC_CR_TXEOM;
                }
                r->TXDR = tx_buf[tx_next];
                tx_next++;
            }
            r->ISR = CEC_FLAG_TXBR;
        }

        if ((isr & CEC_FLAG_TXEND) != 0U) {
            r->ISR = CEC_FLAG_TXEND;
            tx_busy = 0;
            tx_complete = 1;
            if (tx_wants_reply) {
                reply_deadline_ms = HAL_GetTick() + REPLY_TIMEOUT_MS;
                reply_wait = 1;
            }
        }

        if ((isr & CEC_TX_ERRORS) != 0U) {
            r->ISR = isr & CEC_TX_ERRORS;
            tx_busy = 0;
            tx_error = isr & CEC_TX_ERRORS;
        }

#if FHS_CEC_DEBUG_SHELL
        ev->byte = byte;
        ev_count++;
#else
        (void)byte;
#endif
    }
}

/* --- private --- */

static void cec_msp_init(void)
{
    RCC_PeriphCLKInitTypeDef periph = {0};
    GPIO_InitTypeDef gpio = {0};

    /* CEC runs off its own ~32 kHz clock, not PCLK. HSI/244 is the reset
       default; LSE is the other option and the board has no crystal. */
    periph.PeriphClockSelection = RCC_PERIPHCLK_CEC;
    periph.CecClockSelection = RCC_CECCLKSOURCE_HSI;
    if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK) {
        Error_Handler();
    }

    BOARD_CEC_CLK_ENABLE();
    BOARD_CEC_PORT_CLK_ENABLE();

    cec_pin_to_af();

    HAL_NVIC_SetPriority(BOARD_CEC_IRQn, BOARD_CEC_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(BOARD_CEC_IRQn);
}

/* RM0091 §31.7.1: "CECEN=0 ... aborts any on-going reception or transmission".
   An aborted frame never reaches RXEND or TXEND, so the byte counts stay
   wherever they stopped. Clearing CEC_CR does not clear CEC_ISR either, so a
   pending flag would survive the restart and re-fire the moment we enable. */
static void cec_rearm(void)
{
    rx_len = 0;
    tx_busy = 0;
    tx_complete = 0;
    tx_error = 0;
    reply_wait = 0;
    BOARD_CEC->ISR = CEC_FLAG_ALL;
}

static void cec_pin_to_af(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = BOARD_CEC_PIN;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = BOARD_CEC_PULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = BOARD_CEC_AF;
    HAL_GPIO_Init(BOARD_CEC_PORT, &gpio);
}

void cec_bus_suspend(void)
{
    __HAL_CEC_DISABLE(&cec);
    tx_abandon();
}

void cec_bus_resume(void)
{
    cec_pin_to_af();

    /* The suspend aborted whatever was arriving, so re-arm before listening
       again or the next frame appends to the last one's remains. */
    cec_rearm();
    __HAL_CEC_ENABLE(&cec);
}
