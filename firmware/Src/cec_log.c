#include "cec_log.h"
#include "main.h"

#include <stdio.h>

static int show_all = 0;
static int show_raw = 1;

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
    { 0x47, "Set OSD Name" },
    { 0x70, "System Audio Mode Request" },
    { 0x71, "Give Audio Status" },
    { 0x72, "Set System Audio Mode" },
    { 0x7A, "Report Audio Status" },
    { 0x7D, "Give System Audio Mode Status" },
    { 0x7E, "System Audio Mode Status" },
    { 0x80, "Routing Change" },
    { 0x81, "Routing Information" },
    { 0x82, "Active Source" },
    { 0x83, "Give Physical Address" },
    { 0x84, "Report Physical Address" },
    { 0x85, "Request Active Source" },
    { 0x86, "Set Stream Path" },
    { 0x87, "Device Vendor ID" },
    { 0x89, "Vendor Command" },
    { 0x8C, "Give Device Vendor ID" },
    { 0x8F, "Give Device Power Status" },
    { 0x90, "Report Power Status" },
    { 0x9D, "Inactive Source" },
    { 0x9E, "CEC Version" },
    { 0x9F, "Get CEC Version" },
    { 0xA0, "Vendor Command With ID" },
    { 0xA3, "Report Short Audio Descriptor" },
    { 0xA4, "Request Short Audio Descriptor" },
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

void cec_log_set_show_all(int on)
{
    show_all = on;
}

int cec_log_get_show_all(void)
{
    return show_all;
}

void cec_log_set_show_raw(int on)
{
    show_raw = on;
}

int cec_log_get_show_raw(void)
{
    return show_raw;
}

static int is_ours(uint8_t la)
{
    return (cec_get_ack_mask() & (1U << la)) != 0U;
}

static void stamp(uint32_t ms)
{
    printf("[%4lu.%03lu] ", (unsigned long)(ms / 1000U), (unsigned long)(ms % 1000U));
}

static const char *addr_name(uint8_t la)
{
    static const char *const names[CEC_ADDR_COUNT] = {
        "TV", "Rec1", "Rec2", "Tuner1", "Play1", "Audio", "Tuner2", "Tuner3",
        "Play2", "Rec3", "Tuner4", "Play3", "Rsvd", "Rsvd", "Free", "all",
    };

    if (la != CEC_ADDR_BROADCAST && is_ours(la)) {
        return "us";
    }
    return names[la & 0x0FU];
}

static void print_pa(uint8_t hi, uint8_t lo)
{
    printf("%x.%x.%x.%x", hi >> 4, hi & 0x0FU, lo >> 4, lo & 0x0FU);
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

static const char *device_type_name(uint8_t t)
{
    switch (t) {
    case 0: return "TV";
    case 1: return "Recorder";
    case 3: return "Tuner";
    case 4: return "Playback";
    case 5: return "Audio System";
    case 6: return "Switch";
    default: return "?";
    }
}

static const char *ui_key_name(uint8_t k)
{
    switch (k) {
    case 0x00: return "Select";
    case 0x01: return "Up";
    case 0x02: return "Down";
    case 0x03: return "Left";
    case 0x04: return "Right";
    case 0x09: return "Root Menu";
    case 0x0D: return "Exit";
    case 0x40: return "Power";
    case 0x41: return "Volume Up";
    case 0x42: return "Volume Down";
    case 0x43: return "Mute";
    case 0x44: return "Play";
    case 0x45: return "Stop";
    case 0x46: return "Pause";
    case 0x6C: return "Power Off";
    case 0x6D: return "Power On";
    default:   return "key";
    }
}

static const char *abort_reason_name(uint8_t r)
{
    switch (r) {
    case 0: return "unrecognised opcode";
    case 1: return "not in correct mode";
    case 2: return "cannot provide source";
    case 3: return "invalid operand";
    case 4: return "refused";
    case 5: return "unable to determine";
    default: return "?";
    }
}

/* What the operands say, in words. p[0] is the opcode. */
static void print_meaning(const uint8_t *p, uint8_t len)
{
    if (len == 0U) {
        printf("poll");
        return;
    }
    printf("%s", opcode_name(p[0]));

    switch (p[0]) {
    case 0x82:                      /* Active Source */
    case 0x86:                      /* Set Stream Path */
    case 0x9D:                      /* Inactive Source */
    case 0x81:                      /* Routing Information */
        if (len >= 3U) {
            printf(" ");
            print_pa(p[1], p[2]);
        }
        break;
    case 0x84:                      /* Report Physical Address */
        if (len >= 4U) {
            printf(" ");
            print_pa(p[1], p[2]);
            printf(" %s", device_type_name(p[3]));
        }
        break;
    case 0x80:                      /* Routing Change old -> new */
        if (len >= 5U) {
            printf(" ");
            print_pa(p[1], p[2]);
            printf(" -> ");
            print_pa(p[3], p[4]);
        }
        break;
    case 0x90:                      /* Report Power Status */
        if (len >= 2U) {
            printf(": %s", power_state_name(p[1]));
        }
        break;
    case 0x9E:                      /* CEC Version */
        if (len >= 2U) {
            printf(" %s", p[1] == 0x04 ? "1.3a" : p[1] == 0x05 ? "1.4" :
                          p[1] == 0x06 ? "2.0" : "?");
        }
        break;
    case 0x47:                      /* Set OSD Name */
    case 0x32:                      /* Set Menu Language */
        printf(" \"");
        for (uint8_t i = 1; i < len; i++) {
            printf("%c", (p[i] >= 0x20U && p[i] < 0x7FU) ? p[i] : '.');
        }
        printf("\"");
        break;
    case 0x87:                      /* Device Vendor ID */
        if (len >= 4U) {
            printf(" %02x:%02x:%02x", p[1], p[2], p[3]);
        }
        break;
    case 0x44:                      /* User Control Pressed */
        if (len >= 2U) {
            printf(": %s (%02x)", ui_key_name(p[1]), p[1]);
        }
        break;
    case 0x72:                      /* Set System Audio Mode */
    case 0x7E:                      /* System Audio Mode Status */
        if (len >= 2U) {
            printf(": %s", p[1] ? "on" : "off");
        }
        break;
    case 0x7A:                      /* Report Audio Status */
        if (len >= 2U) {
            printf(": volume %u%s", p[1] & 0x7FU, (p[1] & 0x80U) ? ", muted" : "");
        }
        break;
    case 0x00:                      /* Feature Abort */
        if (len >= 3U) {
            printf(": %s, %s", opcode_name(p[1]), abort_reason_name(p[2]));
        }
        break;
    default:
        break;
    }
}

/* One frame, both directions. With show_raw:
 *
 *   [  15.453] RX [04 83]
 *                TV    -> us     83                 Give Physical Address
 *   [  15.592] TX [4f 84 10 00 04]  ok
 *                us    -> all    84 10 00 04        Report Physical Address 1.0.0.0 Playback
 *
 * The raw line is the wire, header first, with the result of our own
 * transmit; the indented line is what it means. Without show_raw the two
 * collapse into one. */
static void print_line(uint32_t ms, int ours, const uint8_t *frame, uint8_t len,
                       const char *tag)
{
    uint8_t from = frame[0] >> 4;
    uint8_t to = frame[0] & 0x0FU;
    const uint8_t *payload = &frame[1];
    uint8_t plen = (uint8_t)(len - 1U);
    int col = 0;

    stamp(ms);
    if (show_raw) {
        printf("%s [", ours ? "TX" : "RX");
        for (uint8_t i = 0; i < len; i++) {
            printf(i ? " %02x" : "%02x", frame[i]);
        }
        printf("]");
        if (tag != NULL) {
            printf("  %s", tag);
        }
        printf("\n              ");     /* under the bytes, past the stamp */
    }

    printf("%-5s -> %-5s  ", addr_name(from), addr_name(to));
    for (uint8_t i = 0; i < plen; i++) {
        col += printf("%02x ", payload[i]);
    }
    while (col < 18) {              /* six bytes wide before the meaning */
        col += printf(" ");
    }
    print_meaning(payload, plen);
    if (!show_raw && tag != NULL) {
        printf("   %s", tag);
    }
    printf("\n");
}

void cec_log_rx(const cec_frame_t *f)
{
    uint8_t dest = f->data[0] & 0x0FU;

    if (show_all || dest == CEC_ADDR_BROADCAST || is_ours(dest)) {
        print_line(f->ms, 0, f->data, f->len, NULL);
    }
}

void cec_log_tx(const uint8_t *frame, uint8_t len, cec_tx_result_t r, const char *tag)
{
    /* On a broadcast the ack slot is inverted: silence accepts, and a low is
       a follower objecting. "no ack" would read backwards. */
    int broadcast = (frame[0] & 0x0FU) == CEC_ADDR_BROADCAST;
    char text[48];
    const char *why;

    switch (r) {
    case CEC_TX_OK:        why = "ok"; break;
    case CEC_TX_NACK:      why = broadcast ? "REJECTED by a follower" : "NO ACK"; break;
    case CEC_TX_ARB_LOST:  why = "ARBITRATION LOST"; break;
    case CEC_TX_LOW_DRIVE: why = "FAILED, collision on the line"; break;
    default:               why = "FAILED"; break;
    }
    if (tag != NULL) {
        snprintf(text, sizeof(text), "%s (%s)", why, tag);
    } else {
        snprintf(text, sizeof(text), "%s", why);
    }
    print_line(HAL_GetTick(), 1, frame, len, text);
}
