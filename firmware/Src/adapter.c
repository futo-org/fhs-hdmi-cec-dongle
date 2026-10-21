#include "adapter.h"
#include "cec.h"
#include "cec_log.h"
#include "led.h"
#include "main.h"
#include "settings.h"
#include "usb.h"
#include "version.h"

#include "fhs-cec-proto.h"

#include <stddef.h>
#include <string.h>

_Static_assert(CEC_TX_TIMEOUT_MS <= FHS_CEC_TX_BUDGET_MS,
               "a transmit must resolve inside the budget the driver relies on");
_Static_assert(CEC_FRAME_MAX == FHS_CEC_FRAME_MAX, "frame sizes disagree");
_Static_assert(sizeof(struct fhs_cec_ev_rx) < FHS_CEC_EVENT_PACKET_SIZE,
               "an event must end its transfer with a short packet");

#define EVENT_QUEUE 16U         /* power of two */

typedef struct {
    uint8_t len;
    uint8_t data[sizeof(struct fhs_cec_ev_rx)];
} event_t;

/* Whose result the frame in flight is. */
typedef enum {
    TX_OWNER_NONE,
    TX_OWNER_HOST,
    TX_OWNER_LOCAL,
} tx_owner_t;

static int enabled;
static int monitor_all;

static event_t events[EVENT_QUEUE];
static uint8_t ev_head;
static uint8_t ev_tail;
static uint32_t events_dropped;

/* Kept out of the event queue so received frames cannot crowd it out. */
static uint8_t tx_done_valid;
static struct fhs_cec_ev_tx_done tx_done;

static tx_owner_t tx_owner;
static uint8_t tx_seq;
static uint8_t tx_frame[CEC_FRAME_MAX];
static uint8_t tx_len;

/* Control transfer buffers must outlive the request. */
static struct fhs_cec_info info;
static struct fhs_cec_status status;
static uint8_t host_frame[CEC_FRAME_MAX];

#if FHS_CEC_DEBUG_SHELL
#define LATE_TX_DONE_MS 2500U   /* past the kernel core's 2100 ms timeout */

static adapter_fault_t fault;
static uint8_t late_valid;
static uint32_t late_at_ms;
static struct fhs_cec_ev_tx_done late_event;
#endif

static void queue_event(const void *data, uint8_t len)
{
    uint8_t next = (uint8_t)((ev_head + 1U) % EVENT_QUEUE);

    if (next == ev_tail) {
        events_dropped++;
        return;
    }
    events[ev_head].len = len;
    memcpy(events[ev_head].data, data, len);
    ev_head = next;
}

static void clear_events(void)
{
    ev_tail = ev_head;
    tx_done_valid = 0;
}

static void queue_tx_done(const struct fhs_cec_ev_tx_done *ev)
{
    tx_done = *ev;
    tx_done_valid = 1;
}

static void send_next_event(void)
{
    if (tx_done_valid) {
        if (usb_event_send((const uint8_t *)&tx_done, sizeof(tx_done))) {
            tx_done_valid = 0;
        }
        return;
    }
    if (ev_tail != ev_head && usb_event_send(events[ev_tail].data, events[ev_tail].len)) {
        ev_tail = (uint8_t)((ev_tail + 1U) % EVENT_QUEUE);
    }
}

static uint8_t tx_status(cec_tx_result_t r)
{
    switch (r) {
    case CEC_TX_OK:        return FHS_CEC_TX_OK;
    case CEC_TX_NACK:      return FHS_CEC_TX_NACK;
    case CEC_TX_ARB_LOST:  return FHS_CEC_TX_ARB_LOST;
    case CEC_TX_LOW_DRIVE: return FHS_CEC_TX_LOW_DRIVE;
    case CEC_TX_ERROR:     return FHS_CEC_TX_ERROR;
    }
    return FHS_CEC_TX_ERROR;
}

static void report_tx(uint8_t seq, uint8_t st)
{
    const struct fhs_cec_ev_tx_done ev = {
        .type = FHS_CEC_EV_TX_DONE,
        .seq = seq,
        .status = st,
    };

#if FHS_CEC_DEBUG_SHELL
    if (fault == ADAPTER_FAULT_WEDGE_TX) {
        fault = ADAPTER_FAULT_NONE;
        return;
    }
    if (fault == ADAPTER_FAULT_LATE_TX_DONE) {
        fault = ADAPTER_FAULT_NONE;
        late_event = ev;
        late_at_ms = HAL_GetTick() + LATE_TX_DONE_MS;
        late_valid = 1;
        return;
    }
#endif
    queue_tx_done(&ev);
}

static int wanted(const cec_frame_t *f)
{
    uint8_t dest = f->data[0] & 0x0FU;

    return monitor_all || dest == CEC_ADDR_BROADCAST ||
           (cec_get_ack_mask() & (1U << dest)) != 0U;
}

static void report_rx(const cec_frame_t *f)
{
    struct fhs_cec_ev_rx ev = {
        .type = FHS_CEC_EV_RX,
        .len = f->len,
    };

#if FHS_CEC_DEBUG_SHELL
    if (fault == ADAPTER_FAULT_DROP_RX) {
        fault = ADAPTER_FAULT_NONE;
        return;
    }
#endif
    memcpy(ev.frame, f->data, f->len);
    queue_event(&ev, (uint8_t)(offsetof(struct fhs_cec_ev_rx, frame) + f->len));
}

static void set_enabled(int on)
{
    enabled = on;
    monitor_all = 0;
    tx_owner = TX_OWNER_NONE;
    clear_events();
    cec_set_ack_mask(0);
    led_set_mode(on ? LED_ON : LED_OFF);
}

void adapter_init(void)
{
    set_enabled(0);
}

void adapter_reset(void)
{
    set_enabled(0);
}

int adapter_enabled(void)
{
    return enabled;
}

static void start_host_transmit(uint8_t seq, uint8_t len)
{
    tx_seq = seq;
    memcpy(tx_frame, host_frame, len);
    tx_len = len;

#if FHS_CEC_DEBUG_SHELL
    if (fault == ADAPTER_FAULT_NACK) {
        fault = ADAPTER_FAULT_NONE;
        tx_owner = TX_OWNER_NONE;
        cec_log_tx(tx_frame, tx_len, CEC_TX_NACK, "fault");
        report_tx(seq, FHS_CEC_TX_NACK);
        return;
    }
#endif
    tx_owner = TX_OWNER_HOST;
    cec_transmit(tx_frame, tx_len);
}

#if FHS_CEC_DEBUG_SHELL
static uint8_t local_initiator(void)
{
    uint16_t mask = cec_get_ack_mask();

    for (uint8_t la = 0; la < CEC_ADDR_BROADCAST; la++) {
        if ((mask & (1U << la)) != 0U) {
            return la;
        }
    }
    return CEC_ADDR_BROADCAST;      /* as an initiator: Unregistered */
}

int adapter_send_local(uint8_t dest, const uint8_t *payload, uint8_t len)
{
    if (dest > CEC_ADDR_BROADCAST || len > CEC_FRAME_MAX - 1U) {
        return 0;
    }
    tx_frame[0] = (uint8_t)((local_initiator() << 4) | dest);
    memcpy(&tx_frame[1], payload, len);
    tx_len = (uint8_t)(len + 1U);
    tx_owner = TX_OWNER_LOCAL;
    return cec_transmit(tx_frame, tx_len);
}
#endif

static void fill_info(void)
{
    info.proto_major = FHS_CEC_PROTO_MAJOR;
    info.proto_minor = FHS_CEC_PROTO_MINOR;
    info.fw_major = FW_VERSION_MAJOR;
    info.fw_minor = FW_VERSION_MINOR;
    info.fw_patch = FW_VERSION_PATCH;
    info.reserved = 0;
    info.phys_addr = settings_phys_addr();
    info.caps = FHS_CEC_CAP_MONITOR_ALL;
}

static void fill_status(void)
{
    cec_stats_t s;

    cec_get_stats(&s);
    status.rx_frames = s.rx_frames;
    status.rx_errors = s.rx_errors;
    status.rx_dropped = s.rx_dropped;
    status.tx_frames = s.tx_frames;
    status.tx_nack = s.tx_nack;
    status.tx_arb_lost = s.tx_arb_lost;
    status.tx_low_drive = s.tx_low_drive;
    status.tx_error = s.tx_error;
    status.events_dropped = events_dropped;
    status.recoveries = s.recoveries;
}

static bool direction_is(tusb_control_request_t const *req, tusb_dir_t dir)
{
    return req->bmRequestType_bit.direction == dir;
}

static bool setup(uint8_t rhport, tusb_control_request_t const *req)
{
    switch (req->bRequest) {
    case FHS_CEC_REQ_GET_INFO:
        TU_VERIFY(direction_is(req, TUSB_DIR_IN));
        fill_info();
        return tud_control_xfer(rhport, req, &info, sizeof(info));

    case FHS_CEC_REQ_GET_STATUS:
        TU_VERIFY(direction_is(req, TUSB_DIR_IN));
        fill_status();
        return tud_control_xfer(rhport, req, &status, sizeof(status));

    case FHS_CEC_REQ_SET_ENABLE:
        TU_VERIFY(direction_is(req, TUSB_DIR_OUT) && req->wLength == 0U);
        set_enabled(req->wValue != 0U);
        return tud_control_status(rhport, req);

    case FHS_CEC_REQ_SET_LOG_ADDRS:
        TU_VERIFY(direction_is(req, TUSB_DIR_OUT) && req->wLength == 0U);
        cec_set_ack_mask(req->wValue);
        return tud_control_status(rhport, req);

    case FHS_CEC_REQ_SET_MONITOR_ALL:
        TU_VERIFY(direction_is(req, TUSB_DIR_OUT) && req->wLength == 0U);
        monitor_all = req->wValue != 0U;
        return tud_control_status(rhport, req);

    case FHS_CEC_REQ_SET_PHYS_ADDR:
        TU_VERIFY(direction_is(req, TUSB_DIR_OUT) && req->wLength == 0U);
        TU_VERIFY(settings_store_phys_addr(req->wValue));
        return tud_control_status(rhport, req);

    case FHS_CEC_REQ_TRANSMIT:
        TU_VERIFY(direction_is(req, TUSB_DIR_OUT) && enabled);
        TU_VERIFY(req->wLength >= 1U && req->wLength <= CEC_FRAME_MAX);
        return tud_control_xfer(rhport, req, host_frame, req->wLength);

    default:
        return false;
    }
}

bool adapter_control(uint8_t rhport, uint8_t stage, tusb_control_request_t const *req)
{
    if (req->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) {
        return false;
    }
    if (stage == CONTROL_STAGE_SETUP) {
        return setup(rhport, req);
    }
    if (stage == CONTROL_STAGE_DATA && req->bRequest == FHS_CEC_REQ_TRANSMIT) {
        TU_VERIFY(tud_control_xferred_len() == req->wLength);
        start_host_transmit((uint8_t)req->wValue, (uint8_t)req->wLength);
    }
    return true;
}

void adapter_task(void)
{
    cec_tx_result_t r;
    cec_frame_t f;

    if (cec_take_tx_result(&r)) {
        if (tx_owner != TX_OWNER_NONE) {
            cec_log_tx(tx_frame, tx_len, r, tx_owner == TX_OWNER_LOCAL ? "local" : NULL);
        }
        if (tx_owner == TX_OWNER_HOST && enabled) {
            report_tx(tx_seq, tx_status(r));
        }
        tx_owner = TX_OWNER_NONE;
    }

    while (cec_take_rx(&f)) {
        cec_log_rx(&f);
        if (enabled && wanted(&f)) {
            report_rx(&f);
        }
    }

#if FHS_CEC_DEBUG_SHELL
    if (late_valid && (HAL_GetTick() - late_at_ms) < 0x80000000UL) {
        late_valid = 0;
        queue_tx_done(&late_event);
    }
#endif

    send_next_event();
}

#if FHS_CEC_DEBUG_SHELL
void adapter_inject_fault(adapter_fault_t f)
{
    fault = f;
}

adapter_fault_t adapter_pending_fault(void)
{
    return fault;
}
#endif
