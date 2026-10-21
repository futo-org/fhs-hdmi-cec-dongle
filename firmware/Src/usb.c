#include "usb.h"
#include "adapter.h"
#include "board.h"
#include "main.h"
#include "version.h"

#include "fhs-cec-proto.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

#include <string.h>

#define EVENT_POLL_INTERVAL_MS 1U
#define MAX_POWER_MA           100U
#define SERIAL_HEX_DIGITS      24U          /* 96-bit unique ID */
#define STRING_MAX_CHARS       32U

enum {
    ITF_NUM_CEC,
#if FHS_CEC_DEBUG_SHELL
    ITF_NUM_CDC_COMM,
    ITF_NUM_CDC_DATA,
#endif
    ITF_NUM_TOTAL
};

enum {
    EP_CEC_EVENTS = 0x81,
    EP_CDC_NOTIFY = 0x82,
    EP_CDC_OUT = 0x03,
    EP_CDC_IN = 0x83,
};

#define CDC_NOTIFY_SIZE 8U
#define CDC_DATA_SIZE   64U

enum {
    STRID_LANGID,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CEC,
    STRID_CDC,
};

#define CEC_ITF_DESC_LEN (sizeof(tusb_desc_interface_t) + sizeof(tusb_desc_endpoint_t))

#if FHS_CEC_DEBUG_SHELL
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CEC_ITF_DESC_LEN + TUD_CDC_DESC_LEN)
#else
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CEC_ITF_DESC_LEN)
#endif

static const tusb_desc_device_t device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
#if FHS_CEC_DEBUG_SHELL
    /* CDC needs an interface association, which needs these */
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
#else
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
#endif
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = FHS_CEC_USB_VID,
    .idProduct = FHS_CEC_USB_PID,
    .bcdDevice = FW_VERSION_BCD,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, MAX_POWER_MA),

    9, TUSB_DESC_INTERFACE, ITF_NUM_CEC, 0, 1,
    FHS_CEC_USB_CLASS, FHS_CEC_USB_SUBCLASS, FHS_CEC_USB_PROTOCOL, STRID_CEC,
    7, TUSB_DESC_ENDPOINT, EP_CEC_EVENTS, TUSB_XFER_INTERRUPT,
    U16_TO_U8S_LE(FHS_CEC_EVENT_PACKET_SIZE), EVENT_POLL_INTERVAL_MS,

#if FHS_CEC_DEBUG_SHELL
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_COMM, STRID_CDC, EP_CDC_NOTIFY, CDC_NOTIFY_SIZE,
                       EP_CDC_OUT, EP_CDC_IN, CDC_DATA_SIZE),
#endif
};

_Static_assert(sizeof(config_desc) == CONFIG_TOTAL_LEN, "configuration length");

static const char *const strings[] = {
    [STRID_MANUFACTURER] = "FUTO",
    [STRID_PRODUCT] = "FHS CEC Adapter",
    [STRID_CEC] = "HDMI-CEC",
    [STRID_CDC] = "FHS CEC debug console",
};

static uint8_t event_ep;
static uint8_t event_buf[FHS_CEC_EVENT_PACKET_SIZE];

/* --- the CEC interface, an application class driver --- */

static void cec_itf_init(void)
{
    event_ep = 0;
}

static bool cec_itf_deinit(void)
{
    return true;
}

static void cec_itf_reset(uint8_t rhport)
{
    (void)rhport;
    event_ep = 0;
    adapter_reset();
}

static uint16_t cec_itf_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len)
{
    tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)tu_desc_next(itf);

    if (itf->bInterfaceClass != FHS_CEC_USB_CLASS ||
        itf->bInterfaceSubClass != FHS_CEC_USB_SUBCLASS ||
        itf->bInterfaceProtocol != FHS_CEC_USB_PROTOCOL ||
        max_len < CEC_ITF_DESC_LEN) {
        return 0;
    }
    if (tu_desc_type(ep) != TUSB_DESC_ENDPOINT || !usbd_edpt_open(rhport, ep)) {
        return 0;
    }
    event_ep = ep->bEndpointAddress;
    return CEC_ITF_DESC_LEN;
}

static bool cec_itf_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result,
                            uint32_t xferred_bytes)
{
    (void)rhport;
    (void)result;
    (void)xferred_bytes;
    return ep_addr == event_ep;
}

static const usbd_class_driver_t cec_itf_driver = {
    .name = "fhs-cec",
    .init = cec_itf_init,
    .deinit = cec_itf_deinit,
    .reset = cec_itf_reset,
    .open = cec_itf_open,
    .control_xfer_cb = adapter_control,
    .xfer_cb = cec_itf_xfer_cb,
    .xfer_isr = NULL,
    .sof = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &cec_itf_driver;
}

/* tinyusb sends every vendor request here, whatever its recipient. */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    return adapter_control(rhport, stage, request);
}

int usb_event_send(const uint8_t *data, uint8_t len)
{
    if (event_ep == 0U || !tud_mounted() || len > sizeof(event_buf)) {
        return 0;
    }
    if (!usbd_edpt_claim(0, event_ep)) {
        return 0;                   /* previous event still in flight */
    }
    memcpy(event_buf, data, len);
    return usbd_edpt_xfer(0, event_ep, event_buf, len, false);
}

/* --- descriptors --- */

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&device_desc;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return config_desc;
}

static uint8_t serial_number(uint16_t *out)
{
    static const char hex[] = "0123456789ABCDEF";
    const uint32_t uid[] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };

    for (uint8_t i = 0; i < SERIAL_HEX_DIGITS; i++) {
        uint32_t word = uid[i / 8U];

        out[i] = (uint16_t)hex[(word >> (28U - 4U * (i % 8U))) & 0xFU];
    }
    return SERIAL_HEX_DIGITS;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static uint16_t desc[1 + STRING_MAX_CHARS];
    uint8_t chars;

    (void)langid;

    if (index == STRID_LANGID) {
        desc[1] = 0x0409;           /* English (US) */
        chars = 1;
    } else if (index == STRID_SERIAL) {
        chars = serial_number(&desc[1]);
    } else if (index < TU_ARRAY_SIZE(strings) && strings[index] != NULL) {
        const char *s = strings[index];

        chars = (uint8_t)strlen(s);
        if (chars > STRING_MAX_CHARS) {
            chars = STRING_MAX_CHARS;
        }
        for (uint8_t i = 0; i < chars; i++) {
            desc[1 + i] = (uint16_t)s[i];
        }
    } else {
        return NULL;
    }

    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2U * chars + 2U));
    return desc;
}

/* --- stack glue --- */

uint32_t tusb_time_millis_api(void)
{
    return HAL_GetTick();
}

void usb_init(void)
{
    const tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };

    __HAL_RCC_USB_CLK_ENABLE();
    HAL_NVIC_SetPriority(BOARD_USB_IRQn, BOARD_USB_IRQ_PRIORITY, 0);
    tusb_init(0, &dev_init);
}

#if FHS_CEC_DEBUG_SHELL
/* printf from inside a TinyUSB callback must not re-enter tud_task. */
static int in_task;
#endif

void usb_task(void)
{
#if FHS_CEC_DEBUG_SHELL
    in_task = 1;
    tud_task();
    in_task = 0;
#else
    tud_task();
#endif
}

void usb_irq_handler(void)
{
    tud_int_handler(0);
}

/* --- debug console --- */

#if FHS_CEC_DEBUG_SHELL
#define CONSOLE_STALL_MS 50U    /* a reader that stopped draining loses output */

int usb_console_getc(void)
{
    uint8_t c;

    if (tud_cdc_read(&c, 1) != 1U) {
        return -1;
    }
    return c;
}

static void console_put(uint8_t c)
{
    uint32_t start = HAL_GetTick();

    while (tud_cdc_write_char((char)c) == 0U) {
        tud_cdc_write_flush();
        if (in_task || !tud_cdc_connected() ||
            (HAL_GetTick() - start) > CONSOLE_STALL_MS) {
            return;
        }
        usb_task();
    }
}

/* Overrides the weak one in syscall.c, which goes a char at a time. */
int _write(int file, char *ptr, int len)
{
    (void)file;

    for (int i = 0; i < len; i++) {
        if (ptr[i] == '\n') {
            console_put('\r');
        }
        console_put((uint8_t)ptr[i]);
    }
    tud_cdc_write_flush();
    return len;
}

int __io_putchar(int ch)
{
    char c = (char)ch;

    _write(1, &c, 1);
    return ch;
}
#endif
