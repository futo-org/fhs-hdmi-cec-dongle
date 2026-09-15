#include "cec.h"
#include "board.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

#define RX_QUEUE       8U       /* power of two */

/* Longest legal frame is 16 bytes at ~24 ms each, plus signal free time and
   the hardware's own retries. Past this a transmit is wedged, not slow. */
#define TX_TIMEOUT_MS  1000U

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

#define OVR_LOG 4U              /* last few overruns, with context */
#define EV_LOG  32U             /* ISR passes, newest last; power of two */

typedef struct {
    uint32_t ms;
    uint8_t len;
    uint8_t data[CEC_FRAME_MAX];
} frame_t;

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

/* SPSC, same split as the uart ring: ISR owns head, main owns tail. */
static frame_t rx_queue[RX_QUEUE];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;

static volatile uint32_t rx_frames;
static volatile uint32_t rx_dropped;
static volatile uint32_t tx_frames;
static volatile uint32_t errors;

/* Frame being transmitted: opcode + operands, the header goes in cec_send. */
static uint8_t tx_buf[CEC_FRAME_MAX];
static volatile uint8_t tx_len;
static volatile uint8_t tx_next;
static volatile uint8_t tx_busy;        /* TXSOM issued, no end seen yet */
static volatile uint8_t tx_wants_reply; /* directed query: an answer is due */
static uint8_t tx_is_reply;             /* auto-response, retry once if lost */
static uint8_t tx_retry_left;
static uint32_t tx_retry_at_ms;
static uint8_t tx_retry_len;            /* the frame kept for the retry */
static uint8_t tx_retry_dest;
static uint8_t tx_retry_buf[CEC_FRAME_MAX];
static volatile uint8_t reply_wait;     /* acked query, nothing back yet */
static volatile uint32_t reply_deadline_ms;
static volatile uint8_t tx_pending;     /* main-loop view, cleared on report */
static uint8_t tx_dest;
static volatile uint8_t tx_complete;
static volatile uint32_t tx_error;
static volatile uint32_t rx_error;
static volatile uint32_t rx_error_count;
static volatile uint32_t rx_overrun_streak;
static volatile uint32_t rx_last_frame_ms;
static volatile uint32_t rx_last_overrun_ms;
static uint32_t rx_error_reported_ms;
static uint32_t tx_started_ms;

/* Instrumentation for the overrun hunt. */
static volatile uint32_t stat_rxbr_during_tx;   /* bytes received while sending */
static volatile uint32_t stat_late_flags;       /* flags found on an ISR rescan */
static volatile uint32_t stat_arb_lost;
static volatile uint32_t stat_rx_resync;        /* stale bytes thrown away */
static volatile uint32_t stat_rx_bad_len;
static volatile uint32_t stat_watchdog;         /* re-inits for a missing reply */
static volatile uint32_t stat_foreign_nack;     /* someone else's frame, unanswered */
static ovr_t ovr_log[OVR_LOG];
static volatile uint8_t ovr_count;
static ev_t ev_log[EV_LOG];
static volatile uint32_t ev_count;

static uint8_t own_addr = CEC_ADDR_NONE;

/* Logical address allocation, CEC 1.4 §10.2.1. A Playback Device tries 4,
   8 and 11 in that order, polling each: an acknowledged poll means the
   address is taken, silence means it is free and ours. Runs at boot, a
   second in so the bus and the TV have settled, and on `cec addr auto`. */
static const uint8_t playback_addrs[] = { 0x4, 0x8, 0xB };
static uint8_t alloc_idx;
static uint8_t alloc_active;
static uint8_t alloc_polling;       /* the frame in flight is our poll */
static uint8_t alloc_tries;         /* per candidate, for bus errors */
static uint32_t alloc_next_ms;
static uint8_t report_due;          /* <Report Physical Address> after claim */

#define ALLOC_BOOT_DELAY_MS 1000U
#define ALLOC_RETRY_MS      300U
#define ALLOC_TRIES_MAX     3U

/* Device identity. There is no EDID reader here, so the physical address is
   declared rather than discovered; the TV's <Routing Change> says the port
   this cable is in is 1.0.0.0. */
#define CEC_PHYS_ADDR    0x1000U
#define CEC_DEVICE_TYPE  0x04U       /* Playback Device, matches address 4 */
#define CEC_VENDOR_ID    0x000000UL

static const char osd_name[] = "FUTO Home";
static int auto_respond = 1;
static int active_source = 0;   /* the TV is showing our port, as far as we know */

/* Power commands to the TV, with retry. LG refuses directed frames for a
   while after it starts a power transition: <Image View On> sent soon after
   <Standby> comes back "tx no ack", and a second try a few seconds later is
   accepted. So the two power commands keep trying, a second apart, for as
   long as the TV takes to settle. */
static uint8_t pwr_opcode;      /* 0x04 or 0x36, 0 when idle */
static uint8_t pwr_in_flight;
static uint8_t pwr_attempts;
static uint8_t pwr_retry_due;
static uint32_t pwr_retry_at_ms;

static uint8_t tv_query_open;   /* <Give Device Power Status> sent, answer due */

/* Volume, CEC 1.4 §13.12 and §13.15. Keys go as <User Control Pressed>
   then <User Control Released>. While an Audio System has System Audio Mode
   on (its <Set System Audio Mode> broadcasts) it owns the volume and the
   keys go to it, otherwise to the TV. */
#define CEC_ADDR_TV     0x0U
#define CEC_ADDR_AUDIO  0x5U
#define UI_VOLUME_UP    0x41U
#define UI_VOLUME_DOWN  0x42U
#define UI_MUTE         0x43U

static uint8_t sys_audio_mode;  /* soundbar has the volume */
static uint8_t vol_ui;          /* key in progress */
static uint8_t vol_left;        /* presses still to send */
static uint8_t vol_stage;       /* 0 idle, 1 press in flight, 2 release in flight */
static uint32_t tv_query_deadline_ms;

#define TV_QUERY_TIMEOUT_MS 2000U
#define PWR_RETRY_MS     1000U
#define PWR_ATTEMPTS_MAX 30U    /* longer than any transition seen */
static int show_all = 0;        /* print frames for other devices too */

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

/* The TV fires its next frame straight after asking us something, on top of
   our answer. The collision reads as a rejected broadcast. One retry, after
   its frame has gone by, is what CEC 1.4 §7.1 expects an initiator to do. */
#define REPLY_RETRY_DELAY_MS 150U

static void cec_msp_init(void);
static void cec_pin_to_af(void);
static void cec_rx_rearm(void);

#define CEC_RX_ERRORS (CEC_FLAG_RXOVR | CEC_FLAG_BRE | CEC_FLAG_SBPE | \
                       CEC_FLAG_LBPE | CEC_FLAG_RXACKE)
#define CEC_TX_ERRORS (CEC_FLAG_TXUDR | CEC_FLAG_TXERR | CEC_FLAG_TXACKE)

/* Every status flag, for clearing them in one go. */
#define CEC_FLAG_ALL (CEC_FLAG_RXBR | CEC_FLAG_RXEND | CEC_RX_ERRORS | \
                      CEC_FLAG_ARBLST | CEC_FLAG_TXBR | CEC_FLAG_TXEND | \
                      CEC_TX_ERRORS)

void cec_init(void)
{
    cec.Instance = BOARD_CEC;

    /* SFT by hardware: a fixed one collides on a two-node bus. */
    cec.Init.SignalFreeTime = CEC_DEFAULT_SFT;
    cec.Init.Tolerance = CEC_STANDARD_TOLERANCE;

    /* Every receive error aborts the frame, so the byte count can be reset on
       any of them. Without this a bit rising error lets reception continue
       and the frame arrives with a hole in it. */
    cec.Init.BRERxStop = CEC_RX_STOP_ON_BRE;

    /* Do not drive error bits: nothing here answers for them yet. */
    cec.Init.BREErrorBitGen = CEC_BRE_ERRORBIT_NO_GENERATION;
    cec.Init.LBPEErrorBitGen = CEC_LBPE_ERRORBIT_NO_GENERATION;
    cec.Init.BroadcastMsgNoErrorBitGen = CEC_BROADCASTERROR_NO_ERRORBIT_GENERATION;

    cec.Init.SignalFreeTimeOption = CEC_SFT_START_ON_TXSOM;

    /* Full listening: every frame on the bus is received and the address
       filter is applied in software. Reduced listening looked like the
       natural choice and is what the TV-only bench ran on, but with a second
       device on the bus (the TV's soundbar, address 5) the receiver went deaf
       within seconds of every TV power transition: no flag, no error, the TV
       still acknowledging us, nothing delivered until a re-init. Whatever the
       hardware does with a directed frame it is meant to ignore, it does not
       do it reliably. Receiving them all costs a few interrupts and avoids
       that path entirely. See README, "Known rough edges". */
    cec.Init.ListenMode = CEC_FULL_LISTENING_MODE;
    cec.Init.OwnAddress = CEC_OWN_ADDRESS_NONE;
    cec.Init.RxBuffer = rx_buf;     /* unused: the HAL IRQ handler is not */

    cec_msp_init();
    if (HAL_CEC_Init(&cec) != HAL_OK) {
        Error_Handler();
    }
    cec_rx_rearm();

    cec_allocate_address();
    alloc_next_ms = HAL_GetTick() + ALLOC_BOOT_DELAY_MS;
}

uint8_t cec_get_address(void)
{
    return own_addr;
}

int cec_set_address(uint8_t logical)
{
    uint16_t oar;

    if (logical == CEC_ADDR_NONE) {
        oar = CEC_OWN_ADDRESS_NONE;
    } else if (logical <= CEC_ADDR_BROADCAST) {
        oar = (uint16_t)(1U << logical);
    } else {
        return 0;
    }

    if (HAL_CEC_SetDeviceAddress(&cec, oar) != HAL_OK) {
        return 0;
    }
    alloc_active = 0;               /* a hand-set address wins */

    /* SetDeviceAddress pokes CFGR directly and leaves Init.OwnAddress stale,
       so a later re-init would silently drop the address. Keep them together. */
    cec.Init.OwnAddress = oar;
    own_addr = logical;

    /* SetDeviceAddress brackets its write with CECEN 0 then 1, which aborts
       any frame in flight. Same cleanup as a re-init. */
    cec_rx_rearm();
    return 1;
}

void cec_set_auto_respond(int on)
{
    auto_respond = on;
}

int cec_get_auto_respond(void)
{
    return auto_respond;
}

void cec_recover(void)
{
    /* HAL_CEC_Init disables the peripheral and rewrites CFGR and IER from
       Init. MspInit is skipped once gState left RESET. */
    if (HAL_CEC_Init(&cec) != HAL_OK) {
        Error_Handler();
    }
    cec_rx_rearm();                 /* Init aborted any frame in flight */
    rx_overrun_streak = 0;
}

int cec_get_extended_tolerance(void)
{
    return cec.Init.Tolerance == CEC_EXTENDED_TOLERANCE;
}

int cec_set_extended_tolerance(int on)
{
    cec.Init.Tolerance = on ? CEC_EXTENDED_TOLERANCE : CEC_STANDARD_TOLERANCE;
    if (HAL_CEC_Init(&cec) != HAL_OK) {
        return 0;
    }
    cec_rx_rearm();
    return 1;
}

int cec_get_listen_all(void)
{
    return cec.Init.ListenMode == CEC_FULL_LISTENING_MODE;
}

int cec_set_listen_all(int on)
{
    uint32_t mode = on ? CEC_FULL_LISTENING_MODE : CEC_REDUCED_LISTENING_MODE;

    if (mode == cec.Init.ListenMode) {
        return 1;                   /* no needless re-init */
    }
    cec.Init.ListenMode = mode;
    if (HAL_CEC_Init(&cec) != HAL_OK) {
        return 0;
    }
    cec_rx_rearm();
    return 1;
}

void cec_set_show_all(int on)
{
    show_all = on;
}

int cec_get_show_all(void)
{
    return show_all;
}

static int send_from(uint8_t initiator, uint8_t dest, const uint8_t *payload,
                     size_t len)
{
    CEC_TypeDef *r = BOARD_CEC;

    if (initiator > CEC_ADDR_BROADCAST || dest > CEC_ADDR_BROADCAST) {
        return 0;
    }
    if (len > CEC_FRAME_MAX - 1U) {
        return 0;
    }
    if (tx_pending || tx_busy) {
        return 0;
    }

    if (len > 0U) {
        memcpy(tx_buf, payload, len);
    }
    tx_len = (uint8_t)len;
    tx_next = 0;

    tx_complete = 0;
    tx_error = 0;
    tx_pending = 1;
    tx_busy = 1;
    tx_dest = dest;

    tx_wants_reply = 0;
    if (dest != CEC_ADDR_BROADCAST && len > 0U) {
        for (size_t i = 0; i < sizeof(query_opcodes); i++) {
            if (payload[0] == query_opcodes[i]) {
                tx_wants_reply = 1;
            }
        }
    }
    tx_started_ms = HAL_GetTick();

    /* RM0091 §31.7.1: TXEOM before TXDR, TXDR before TXSOM. A header-only
       frame (a poll) is its own last byte. */
    if (len == 0U) {
        r->CR |= CEC_CR_TXEOM;
    }
    r->TXDR = (uint32_t)((uint32_t)initiator << 4) | dest;
    r->CR |= CEC_CR_TXSOM;
    return 1;
}

int cec_send(uint8_t dest, const uint8_t *payload, size_t len)
{
    /* The header carries the initiator, so an address is not optional. */
    if (own_addr == CEC_ADDR_NONE) {
        return 0;
    }
    return send_from(own_addr, dest, payload, len);
}

void cec_allocate_address(void)
{
    alloc_idx = 0;
    alloc_tries = 0;
    alloc_polling = 0;
    alloc_active = 1;
    alloc_next_ms = HAL_GetTick();
}

int cec_allocating(void)
{
    return alloc_active;
}

/* Poll the next candidate. The poll goes out from the address it asks
   about, so nothing here needs an own address yet. */
static void alloc_step(void)
{
    uint8_t cand;

    if (alloc_idx >= sizeof(playback_addrs)) {
        alloc_active = 0;
        printf("cec: no free playback address (4, 8, b all taken), "
               "use 'cec addr <n>'\n");
        return;
    }
    cand = playback_addrs[alloc_idx];
    if (send_from(cand, cand, NULL, 0)) {
        alloc_polling = 1;
    } else {
        alloc_next_ms = HAL_GetTick() + ALLOC_RETRY_MS;
    }
}

static void alloc_claim(uint8_t addr)
{
    alloc_active = 0;
    if (!cec_set_address(addr)) {
        printf("cec: could not set address %x\n", addr);
        return;
    }
    printf("cec: logical address %x (playback), physical %x.%x.%x.%x\n", addr,
           (CEC_PHYS_ADDR >> 12) & 0xFU, (CEC_PHYS_ADDR >> 8) & 0xFU,
           (CEC_PHYS_ADDR >> 4) & 0xFU, CEC_PHYS_ADDR & 0xFU);
    report_due = 1;                 /* tell the TV we exist */
}

/* --- reporting, main loop only --- */

static const struct {
    uint8_t opcode;
    const char *name;
} opcode_names[] = {
    { 0x00, "Feature Abort" },
    { 0x04, "Image View On" },
    { 0x0D, "Text View On" },
    { 0x32, "Set Menu Language" },
    { 0x36, "Standby" },
    { 0x44, "User Control Pressed" },
    { 0x45, "User Control Released" },
    { 0x46, "Give OSD Name" },
    { 0x71, "Give Audio Status" },
    { 0x7A, "Report Audio Status" },
    { 0x70, "System Audio Mode Request" },
    { 0x72, "Set System Audio Mode" },
    { 0x7D, "Give System Audio Mode Status" },
    { 0x7E, "System Audio Mode Status" },
    { 0x80, "Routing Change" },
    { 0x81, "Routing Information" },
    { 0x86, "Set Stream Path" },
    { 0x89, "Vendor Command" },
    { 0x9D, "Inactive Source" },
    { 0xA0, "Vendor Command With ID" },
    { 0xA3, "Report Short Audio Descriptor" },
    { 0xA4, "Request Short Audio Descriptor" },
    { 0x47, "Set OSD Name" },
    { 0x82, "Active Source" },
    { 0x83, "Give Physical Address" },
    { 0x84, "Report Physical Address" },
    { 0x85, "Request Active Source" },
    { 0x87, "Device Vendor ID" },
    { 0x8C, "Give Device Vendor ID" },
    { 0x8F, "Give Device Power Status" },
    { 0x90, "Report Power Status" },
    { 0x9E, "CEC Version" },
    { 0x9F, "Get CEC Version" },
    { 0xFF, "Abort" },
};

#define OPCODE_COUNT (sizeof(opcode_names) / sizeof(opcode_names[0]))

static const char *opcode_name(uint8_t opcode)
{
    for (size_t i = 0; i < OPCODE_COUNT; i++) {
        if (opcode_names[i].opcode == opcode) {
            return opcode_names[i].name;
        }
    }
    return "?";
}

/* One name per error bit, so a failure says which one. */
static const struct {
    uint32_t bit;
    const char *name;
} error_names[] = {
    { CEC_FLAG_RXOVR,  "rx overrun" },
    { CEC_FLAG_BRE,    "bit rising" },
    { CEC_FLAG_SBPE,   "short bit" },
    { CEC_FLAG_LBPE,   "long bit" },
    { CEC_FLAG_RXACKE, "rx no ack" },
    { CEC_FLAG_ARBLST, "arbitration lost" },
    { CEC_FLAG_TXUDR,  "tx underrun" },
    { CEC_FLAG_TXERR,  "tx error" },
    { CEC_FLAG_TXACKE, "tx no ack" },
};

#define ERROR_COUNT (sizeof(error_names) / sizeof(error_names[0]))

static void print_errors(const char *what, uint32_t bits)
{
    printf("%s", what);
    for (size_t i = 0; i < ERROR_COUNT; i++) {
        if ((bits & error_names[i].bit) != 0U) {
            printf(" %s", error_names[i].name);
        }
    }
    printf(" (0x%lx)\n", (unsigned long)bits);
}

static void print_frame(const frame_t *f)
{
    /* Byte 0 is the header: initiator in the high nibble, destination in the
       low one. The payload starts at byte 1. */
    printf("[%8lu] rx %x->%x", (unsigned long)f->ms,
           f->data[0] >> 4, f->data[0] & 0x0FU);

    for (uint8_t i = 1; i < f->len; i++) {
        printf(" %02x", f->data[i]);
    }

    if (f->len < 2U) {
        printf("  <poll>\n");
    } else {
        printf("  <%s>\n", opcode_name(f->data[1]));
    }
}

/* <Active Source> is how a source claims the screen: the TV routes to the
   physical address in it. Always broadcast. */
static int send_active_source(void)
{
    uint8_t msg[3] = { 0x82, (uint8_t)(CEC_PHYS_ADDR >> 8), (uint8_t)CEC_PHYS_ADDR };

    if (cec_send(CEC_ADDR_BROADCAST, msg, sizeof(msg))) {
        active_source = 1;
        printf("         -> active source %x.%x.%x.%x\n",
               (CEC_PHYS_ADDR >> 12) & 0xFU, (CEC_PHYS_ADDR >> 8) & 0xFU,
               (CEC_PHYS_ADDR >> 4) & 0xFU, CEC_PHYS_ADDR & 0xFU);
        return 1;
    }
    return 0;
}

static int power_command(uint8_t opcode)
{
    if (!cec_send(0x0, &opcode, 1)) {
        return 0;
    }
    pwr_opcode = opcode;
    pwr_in_flight = 1;
    pwr_attempts = 1;
    pwr_retry_due = 0;
    return 1;
}

/* CEC 1.4 §13.1 One Touch Play: <Image View On> wakes the TV, <Active Source>
   makes it switch to our port. The second goes out when the first has been
   acknowledged; a TV in standby ignores broadcasts until it is awake, so the
   responder below repeats it when the TV asks with <Set Stream Path>. */
int cec_one_touch_play(void)
{
    return power_command(0x04);
}

int cec_standby(void)
{
    active_source = 0;
    return power_command(0x36);
}

int cec_is_active_source(void)
{
    return active_source;
}

/* Ask the TV for its power state. The answer, <Report Power Status>, is
   decoded and printed by cec_task when it arrives. */
int cec_tv_query_power(void)
{
    uint8_t q = 0x8F;

    if (!cec_send(0x0, &q, 1)) {
        return 0;
    }
    tv_query_open = 1;
    tv_query_deadline_ms = HAL_GetTick() + TV_QUERY_TIMEOUT_MS;
    return 1;
}

static uint8_t volume_target(void)
{
    return sys_audio_mode ? CEC_ADDR_AUDIO : CEC_ADDR_TV;
}

int cec_volume(uint8_t ui, uint8_t count)
{
    if (own_addr == CEC_ADDR_NONE || vol_stage != 0U || count == 0U) {
        return 0;
    }
    vol_ui = ui;
    vol_left = count;
    vol_stage = 0;                  /* cec_task sends the first press */
    return 1;
}

int cec_volume_up(uint8_t n)   { return cec_volume(UI_VOLUME_UP, n); }
int cec_volume_down(uint8_t n) { return cec_volume(UI_VOLUME_DOWN, n); }
int cec_volume_mute(void)      { return cec_volume(UI_MUTE, 1); }

/* <Give Audio Status>: only an Audio System answers it, with volume and
   mute in one byte. The TV has no such report. */
int cec_audio_status(void)
{
    uint8_t q = 0x71;

    return cec_send(volume_target(), &q, 1);
}

int cec_system_audio_mode(void)
{
    return sys_audio_mode;
}

static const char *power_state_name(uint8_t st)
{
    switch (st) {
    case 0x00: return "on";
    case 0x01: return "standby";
    case 0x02: return "turning on";
    case 0x03: return "turning off";
    default:   return "unknown";
    }
}

/* CEC opcodes we answer. A follower that never reports its physical address
   stays unregistered, and an unregistered device's commands get acknowledged
   and then ignored. */
static void respond(const frame_t *f)
{
    uint8_t reply[CEC_FRAME_MAX];
    uint8_t initiator;
    uint8_t dest;
    size_t n = 0;

    if (!auto_respond || f->len < 2U || own_addr == CEC_ADDR_NONE) {
        return;                     /* a poll is acknowledged in hardware */
    }

    initiator = f->data[0] >> 4;
    dest = f->data[0] & 0x0FU;

    /* Routing, all broadcast. The TV asks "who is at this address" and the
       device there answers <Active Source>; anyone else claiming the screen
       means we are no longer on it. */
    if (dest == CEC_ADDR_BROADCAST) {
        uint16_t phys = 0xFFFFU;

        switch (f->data[1]) {
        case 0x85:                  /* Request Active Source */
            if (active_source) {
                send_active_source();
            }
            break;
        case 0x86:                  /* Set Stream Path [phys] */
            if (f->len >= 4U) {
                phys = (uint16_t)((f->data[2] << 8) | f->data[3]);
            }
            break;
        case 0x80:                  /* Routing Change [old][new] */
            if (f->len >= 6U) {
                phys = (uint16_t)((f->data[4] << 8) | f->data[5]);
            }
            break;
        case 0x82:                  /* Active Source [phys], someone else's */
            if (f->len >= 4U && initiator != own_addr) {
                active_source = 0;
            }
            break;
        default:
            break;
        }
        if (phys != 0xFFFFU) {
            if (phys == CEC_PHYS_ADDR) {
                send_active_source();
            } else {
                active_source = 0;
            }
        }
        return;
    }

    if (dest != own_addr) {
        return;                     /* someone else's business */
    }

    tx_is_reply = 1;
    switch (f->data[1]) {
    case 0x83:                      /* Give Physical Address */
        reply[n++] = 0x84;          /* Report Physical Address, must broadcast */
        reply[n++] = (uint8_t)(CEC_PHYS_ADDR >> 8);
        reply[n++] = (uint8_t)CEC_PHYS_ADDR;
        reply[n++] = CEC_DEVICE_TYPE;
        cec_send(CEC_ADDR_BROADCAST, reply, n);
        break;

    case 0x8C:                      /* Give Device Vendor ID */
        reply[n++] = 0x87;          /* Device Vendor ID, must broadcast */
        reply[n++] = (uint8_t)(CEC_VENDOR_ID >> 16);
        reply[n++] = (uint8_t)(CEC_VENDOR_ID >> 8);
        reply[n++] = (uint8_t)CEC_VENDOR_ID;
        cec_send(CEC_ADDR_BROADCAST, reply, n);
        break;

    case 0x46:                      /* Give OSD Name */
        reply[n++] = 0x47;
        for (size_t i = 0; i < sizeof(osd_name) - 1U && n < CEC_FRAME_MAX - 1U; i++) {
            reply[n++] = (uint8_t)osd_name[i];
        }
        cec_send(initiator, reply, n);
        break;

    case 0x8F:                      /* Give Device Power Status */
        reply[n++] = 0x90;
        reply[n++] = 0x00;          /* On */
        cec_send(initiator, reply, n);
        break;

    case 0x9F:                      /* Get CEC Version */
        reply[n++] = 0x9E;
        reply[n++] = 0x05;          /* 1.4 */
        cec_send(initiator, reply, n);
        break;

    default:
        tx_is_reply = 0;
        return;
    }

    /* Keep a copy: tx_buf is the transmitter's and a retry re-sends it, but
       a user frame could be queued in between. */
    memcpy(tx_retry_buf, reply, n);
    tx_retry_len = (uint8_t)n;
    tx_retry_dest = (f->data[1] == 0x83 || f->data[1] == 0x8C)
                        ? CEC_ADDR_BROADCAST : initiator;
    tx_retry_left = 1;

    printf("         -> reply %02x\n", reply[0]);
}

void cec_task(void)
{
    uint32_t bits;

    if (rx_dropped > 0U) {
        printf("rx queue full, %lu frames dropped\n", (unsigned long)rx_dropped);
        rx_dropped = 0;
    }

    /* A misdecoding bus produces one of these per frame. Coalesce, or the
       console is unusable and the printf backlog makes the decoding worse. */
    bits = rx_error;
    if (bits != 0U && (HAL_GetTick() - rx_error_reported_ms) >= 1000U) {
        uint32_t n = rx_error_count;

        rx_error = 0;
        rx_error_count = 0;
        rx_error_reported_ms = HAL_GetTick();
        printf("rx error x%lu:", (unsigned long)n);
        print_errors("", bits);
    }

    if (tx_complete) {
        tx_complete = 0;
        tx_pending = 0;
        tx_retry_left = 0;          /* got through, nothing to retry */
        tx_is_reply = 0;
        if (alloc_polling) {        /* acknowledged: someone lives there */
            alloc_polling = 0;
            printf("cec: address %x is taken\n", playback_addrs[alloc_idx]);
            alloc_idx++;
            alloc_tries = 0;
            alloc_next_ms = HAL_GetTick();
        } else {
            printf("[%8lu] tx ok\n", (unsigned long)HAL_GetTick());
        }

        if (vol_stage == 1U) {      /* press acknowledged */
            vol_stage = 2;
        } else if (vol_stage == 2U) {   /* release acknowledged */
            vol_left--;
            vol_stage = 0;          /* next press, if any are left */
            if (vol_left == 0U) {
                printf("         volume key done\n");
            }
        }

        if (pwr_in_flight) {        /* TV acknowledged the power command */
            pwr_in_flight = 0;
            if (pwr_attempts > 1U) {
                printf("         tv accepted it on attempt %u\n", pwr_attempts);
            }
            if (pwr_opcode == 0x04U) {
                send_active_source();
            }
            pwr_opcode = 0;
        }
    }

    bits = tx_error;
    if (bits != 0U) {
        tx_error = 0;
        tx_pending = 0;

        if (alloc_polling) {
            alloc_polling = 0;
            if ((bits & CEC_FLAG_TXACKE) != 0U) {
                alloc_claim(playback_addrs[alloc_idx]);     /* silence: free */
            } else if (++alloc_tries < ALLOC_TRIES_MAX) {
                alloc_next_ms = HAL_GetTick() + ALLOC_RETRY_MS;
            } else {
                printf("cec: bus error polling %x, skipping it\n",
                       playback_addrs[alloc_idx]);
                alloc_idx++;
                alloc_tries = 0;
                alloc_next_ms = HAL_GetTick();
            }
            bits = 0;
        }

        if (bits != 0U) {
            printf("[%8lu] ", (unsigned long)HAL_GetTick());
        }
        if (tv_query_open && bits != 0U) {
            tv_query_open = 0;      /* the failure line says why */
        }
        if (vol_stage != 0U && bits != 0U) {
            vol_stage = 0;
            vol_left = 0;
            printf("volume key to %x failed: ", volume_target());
        }

        if (pwr_in_flight && (bits & CEC_FLAG_TXACKE) != 0U &&
            pwr_attempts < PWR_ATTEMPTS_MAX) {
            /* Mid-transition refusal. Try again in a moment. */
            pwr_in_flight = 0;
            pwr_retry_due = 1;
            pwr_retry_at_ms = HAL_GetTick() + PWR_RETRY_MS;
            printf("tv not ready (no ack), retry %u/%u in %u ms\n",
                   pwr_attempts + 1U, PWR_ATTEMPTS_MAX, PWR_RETRY_MS);
            bits = 0;
        } else if (pwr_in_flight) {
            printf("tv refused %02x %u times, giving up: ",
                   pwr_opcode, pwr_attempts);
            pwr_in_flight = 0;
            pwr_opcode = 0;
        }

        /* On a broadcast the ack slot is inverted: silence accepts, and a low
           is a follower objecting. Calling that "no ack" reads backwards. */
        if (bits == 0U) {
            /* already reported above */
        } else if (tx_dest == CEC_ADDR_BROADCAST &&
                   (bits & CEC_FLAG_TXACKE) != 0U) {
            print_errors("tx rejected by a follower:", bits);
        } else {
            print_errors("tx failed:", bits);
        }

        if (tx_is_reply && tx_retry_left > 0U &&
            (bits & (CEC_FLAG_TXACKE | CEC_FLAG_TXERR)) != 0U) {
            tx_retry_at_ms = HAL_GetTick() + REPLY_RETRY_DELAY_MS;
        } else {
            tx_retry_left = 0;
        }
        tx_is_reply = 0;
    }

    if (vol_left > 0U && !tx_pending) {
        uint8_t msg[2];

        if (vol_stage == 0U) {
            msg[0] = 0x44;          /* User Control Pressed */
            msg[1] = vol_ui;
            if (cec_send(volume_target(), msg, 2)) {
                vol_stage = 1;
            }
        } else if (vol_stage == 2U) {
            msg[0] = 0x45;          /* User Control Released */
            if (cec_send(volume_target(), msg, 1)) {
                /* stays 2 until acknowledged, then counts down above */
            }
        }
    }

    if (tv_query_open &&
        (HAL_GetTick() - tv_query_deadline_ms) < 0x80000000UL) {
        tv_query_open = 0;
        printf("         tv did not answer in %u ms\n",
               (unsigned)TV_QUERY_TIMEOUT_MS);
    }

    if (alloc_active && !alloc_polling && !tx_pending &&
        (HAL_GetTick() - alloc_next_ms) < 0x80000000UL) {
        alloc_step();
    }

    if (report_due && !tx_pending && own_addr != CEC_ADDR_NONE) {
        uint8_t msg[4] = { 0x84, (uint8_t)(CEC_PHYS_ADDR >> 8),
                           (uint8_t)CEC_PHYS_ADDR, CEC_DEVICE_TYPE };

        report_due = 0;
        if (cec_send(CEC_ADDR_BROADCAST, msg, sizeof(msg))) {
            printf("         -> report physical address\n");
        }
    }

    if (pwr_retry_due && !tx_pending &&
        (HAL_GetTick() - pwr_retry_at_ms) < 0x80000000UL) {
        pwr_retry_due = 0;
        pwr_attempts++;
        if (cec_send(0x0, &pwr_opcode, 1)) {
            pwr_in_flight = 1;
        } else {
            printf("power command retry could not be queued\n");
            pwr_opcode = 0;
        }
    }

    if (tx_retry_left > 0U && !tx_pending &&
        (HAL_GetTick() - tx_retry_at_ms) < 0x80000000UL) {
        tx_retry_left--;
        tx_is_reply = 1;
        if (cec_send(tx_retry_dest, tx_retry_buf, tx_retry_len)) {
            printf("         -> retry %02x\n", tx_retry_buf[0]);
        } else {
            tx_is_reply = 0;
        }
    }

    /* One frame per pass: a reply needs the transmitter and it is single
       buffered. Anything we are too slow for, the TV asks again. */
    if (!tx_pending && rx_tail != rx_head) {
        const frame_t *f = &rx_queue[rx_tail];
        uint8_t dest = f->data[0] & 0x0FU;

        if (show_all || dest == own_addr || dest == CEC_ADDR_BROADCAST) {
            print_frame(f);
        }
        if (f->len >= 3U && (f->data[0] >> 4) == 0x0U && f->data[1] == 0x90U) {
            printf("         tv is %s\n", power_state_name(f->data[2]));
            tv_query_open = 0;
        }
        if (f->len >= 3U && (f->data[1] == 0x72U || f->data[1] == 0x7EU)) {
            /* Set System Audio Mode / System Audio Mode Status */
            sys_audio_mode = f->data[2] ? 1U : 0U;
        }
        if (f->len >= 3U && f->data[1] == 0x7AU) {
            printf("         audio: volume %u%s\n", f->data[2] & 0x7FU,
                   (f->data[2] & 0x80U) ? ", muted" : "");
        }
        respond(f);
        rx_tail = (uint8_t)((rx_tail + 1U) % RX_QUEUE);
    }

    /* The TV acknowledged a question it must answer and the bus has been
       silent since. Only a deaf receiver produces that; a re-init is the
       known cure and is harmless if the TV was merely rude. */
    if (reply_wait && (HAL_GetTick() - reply_deadline_ms) < 0x80000000UL) {
        reply_wait = 0;
        stat_watchdog++;
        printf("rx watchdog: acked query, no reply in %u ms, re-initialising\n",
               (unsigned)REPLY_TIMEOUT_MS);
        cec_recover();
    }

    /* Let a stale streak decay. A quiet bus (the TV asleep) would otherwise
       satisfy the silence test forever and re-init on an old overrun count. */
    if (rx_overrun_streak > 0U &&
        (HAL_GetTick() - rx_last_overrun_ms) > RX_STUCK_SILENCE_MS) {
        rx_overrun_streak = 0;
    }

    /* Should not happen any more; kept as the last line of defence. */
    if (rx_overrun_streak >= RX_OVERRUN_STREAK_MAX &&
        (HAL_GetTick() - rx_last_frame_ms) > RX_STUCK_SILENCE_MS) {
        printf("rx stuck: %lu overruns, no frame for %lu ms, re-initialising\n",
               (unsigned long)rx_overrun_streak,
               (unsigned long)(HAL_GetTick() - rx_last_frame_ms));
        cec_recover();
    }

    /* Arbitration lost is not a failure: the hardware keeps TXSOM pending and
       retries after the winner's frame. But a bus that never goes quiet, or a
       transmitter wedged some other way, would leave us busy forever. */
    if (tx_pending && (HAL_GetTick() - tx_started_ms) > TX_TIMEOUT_MS) {
        errors++;
        printf("tx timeout after %u ms, state %s, recovering\n",
               (unsigned)TX_TIMEOUT_MS, cec_state_name());
        /* Leaving it wedged means the hardware keeps retrying and jams the
           bus low, which then corrupts every frame anyone else sends. */
        cec_recover();
    }
}

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

uint32_t cec_rx_count(void)
{
    return rx_frames;
}

uint32_t cec_tx_count(void)
{
    return tx_frames;
}

uint32_t cec_err_count(void)
{
    return errors;
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
    printf("rxbr during tx %lu, watchdog %lu, foreign nack %lu\n",
           (unsigned long)stat_rxbr_during_tx, (unsigned long)stat_watchdog,
           (unsigned long)stat_foreign_nack);
    printf("late flags %lu, arb lost %lu, resync %lu, bad len %lu\n",
           (unsigned long)stat_late_flags, (unsigned long)stat_arb_lost,
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

/* --- interrupt path, must not block --- */

/* Copy and get out. printf in here would hold the bus for a character time
   and cost the next frame, the same trap the console RX path documents. */
static void rx_deliver(void)
{
    uint8_t next;
    uint8_t size = rx_len;

    rx_frames++;
    rx_overrun_streak = 0;          /* a whole frame arrived: not stuck */
    rx_last_frame_ms = HAL_GetTick();
    reply_wait = 0;                 /* anything at all proves we can hear */

    if (size == 0U || size > CEC_FRAME_MAX) {
        stat_rx_bad_len++;
        return;
    }

    next = (uint8_t)((rx_head + 1U) % RX_QUEUE);
    if (next == rx_tail) {          /* full: drop, never overwrite */
        rx_dropped++;
        return;
    }

    rx_queue[rx_head].ms = HAL_GetTick();
    rx_queue[rx_head].len = size;
    memcpy(rx_queue[rx_head].data, rx_buf, size);
    rx_head = next;
}

static void rx_fail(uint32_t bits, uint8_t rxdr)
{
    uint32_t now = HAL_GetTick();

    /* Someone else's frame that nobody acknowledged, typically the TV
       polling its soundbar while that is in standby. Nothing of ours was
       lost, so it is counted rather than reported. In full listening mode
       it arrives as RXACKE, documented. In reduced listening mode it arrives
       as RXOVR with no byte delivered and RXDR still holding our previous
       frame's last byte, which is not documented anywhere. */
    if (bits == CEC_FLAG_RXACKE || bits == CEC_FLAG_RXOVR) {
        uint8_t dest = rx_buf[0] & 0x0FU;

        if (rx_len == 0U ||
            (dest != own_addr && dest != CEC_ADDR_BROADCAST)) {
            stat_foreign_nack++;
            rx_len = 0;
            return;
        }
    }

    errors++;
    rx_error |= bits;
    rx_error_count++;

    if ((bits & CEC_FLAG_RXOVR) != 0U) {
        ovr_t *o = &ovr_log[ovr_count % OVR_LOG];

        o->ms = now;
        o->isr = (uint16_t)bits;
        o->cr = (uint8_t)BOARD_CEC->CR;
        o->tx_busy = tx_busy;
        o->rx_len = rx_len;
        o->rxdr = rxdr;
        ovr_count++;

        rx_overrun_streak++;
        rx_last_overrun_ms = now;
    } else {
        rx_overrun_streak = 0;
    }

    /* Every enabled receive error aborts the frame (BRE because BRESTP is
       set). The next byte on the bus starts a new one. */
    rx_len = 0;
}

static void tx_fail(uint32_t bits)
{
    errors++;
    tx_busy = 0;
    tx_error = bits;
}

void cec_irq_handler(void)
{
    CEC_TypeDef *r = BOARD_CEC;
    uint32_t isr;
    int pass = 0;

    /* Rescan until nothing is pending. A flag raised while we were in here
       is served now instead of being lost, and every clear below is a plain
       store of the bits we have actually handled. */
    while ((isr = (r->ISR & r->IER)) != 0U) {
        ev_t *ev = &ev_log[ev_count % EV_LOG];
        uint8_t byte = 0;

        if (pass++ > 0) {
            stat_late_flags++;
        }

        ev->ms = HAL_GetTick();
        ev->isr = (uint16_t)isr;
        ev->rx_len = rx_len;

        if ((isr & CEC_FLAG_RXBR) != 0U) {
            uint32_t now = HAL_GetTick();

            byte = (uint8_t)r->RXDR;

            r->ISR = CEC_FLAG_RXBR;

            if (rx_len > 0U && (now - rx_last_byte_ms) > RX_BYTE_GAP_MAX_MS) {
                stat_rx_resync++;
                rx_len = 0;
            }
            rx_last_byte_ms = now;

            if (rx_len < CEC_FRAME_MAX) {
                rx_buf[rx_len] = byte;
            }
            rx_len++;
            if (tx_busy) {
                stat_rxbr_during_tx++;
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
            stat_arb_lost++;
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
            tx_frames++;
            tx_complete = 1;
            if (tx_wants_reply) {
                reply_deadline_ms = HAL_GetTick() + REPLY_TIMEOUT_MS;
                reply_wait = 1;
            }
        }

        if ((isr & CEC_TX_ERRORS) != 0U) {
            r->ISR = isr & CEC_TX_ERRORS;
            tx_fail(isr & CEC_TX_ERRORS);
        }

        ev->byte = byte;
        ev_count++;
    }
}

/* --- private --- */

static void cec_msp_init(void)
{
    RCC_PeriphCLKInitTypeDef periph = {0};

    /* CEC runs off its own ~32 kHz clock, not PCLK. HSI/244 is the reset
       default; LSE is the other option and the Nucleo has no crystal fitted. */
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
static void cec_rx_rearm(void)
{
    rx_len = 0;
    tx_busy = 0;
    tx_pending = 0;
    reply_wait = 0;
    pwr_in_flight = 0;
    pwr_retry_due = 0;
    alloc_polling = 0;
    BOARD_CEC->ISR = CEC_FLAG_ALL;
}

/* Open drain, no pull: the bus pull-up is external. An internal pull-up would
   fight every other device pulling the line down. */
static void cec_pin_to_af(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = BOARD_CEC_PIN;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = BOARD_CEC_AF;
    HAL_GPIO_Init(BOARD_CEC_PORT, &gpio);
}

void cec_bus_suspend(void)
{
    __HAL_CEC_DISABLE(&cec);
}

void cec_bus_resume(void)
{
    cec_pin_to_af();

    /* The suspend aborted whatever was arriving, so re-arm before listening
       again or the next frame appends to the last one's remains. */
    cec_rx_rearm();
    __HAL_CEC_ENABLE(&cec);
}
