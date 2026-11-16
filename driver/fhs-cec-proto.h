/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Wire protocol between the fhs-cec kernel driver and the dongle firmware.
 * Shared by both sides; keep it free of kernel and HAL headers.
 *
 * Host to device: vendor control requests on EP0, addressed to the CEC
 * interface (wIndex = interface number).
 * Device to host: one event per transfer on the interrupt IN endpoint.
 *
 * Multi-byte fields are little-endian.
 */
#ifndef FHS_CEC_PROTO_H
#define FHS_CEC_PROTO_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define FHS_CEC_USB_VID 0x3a4c
#define FHS_CEC_USB_PID 0x4663

#define FHS_CEC_USB_CLASS    0xFF
#define FHS_CEC_USB_SUBCLASS 0x01
#define FHS_CEC_USB_PROTOCOL 0x01

/* The driver refuses a device with a different major. */
#define FHS_CEC_PROTO_MAJOR 1
#define FHS_CEC_PROTO_MINOR 2

/* Header + opcode + up to 14 operands. */
#define FHS_CEC_FRAME_MAX 16

/* Interrupt IN wMaxPacketSize. Every event is shorter, so each one ends its
   transfer with a short packet. */
#define FHS_CEC_EVENT_PACKET_SIZE 32

/* The firmware reports every transmit within this, well inside the kernel
   core's 2100 ms transmit timeout. */
#define FHS_CEC_TX_BUDGET_MS 1000

#define FHS_CEC_PHYS_ADDR_INVALID 0xFFFF

#define FHS_CEC_EDID_BLOCK_SIZE 128
/* The base block and one extension, the most one DDC segment holds. */
#define FHS_CEC_EDID_MAX_BLOCKS 2

/* The firmware answers GET_EDID within this, inside the driver's control
   request timeout. */
#define FHS_CEC_EDID_BUDGET_MS 600

enum fhs_cec_request {
	/* IN, struct fhs_cec_info */
	FHS_CEC_REQ_GET_INFO = 0x01,
	/* wValue: 1 = adapter enabled, 0 = idle (no ack mask, no events) */
	FHS_CEC_REQ_SET_ENABLE = 0x02,
	/* wValue: bit n set = acknowledge logical address n */
	FHS_CEC_REQ_SET_LOG_ADDRS = 0x03,
	/* wValue: 1 = forward frames addressed to other devices too */
	FHS_CEC_REQ_SET_MONITOR_ALL = 0x04,
	/* wValue: sequence number; data stage: the frame, header first */
	FHS_CEC_REQ_TRANSMIT = 0x05,
	/* wValue: physical address, stored in device flash */
	FHS_CEC_REQ_SET_PHYS_ADDR = 0x06,
	/* IN, struct fhs_cec_status */
	FHS_CEC_REQ_GET_STATUS = 0x07,
	/* IN, wValue: block number; the 128-byte EDID block read from the
	   display over DDC. Stalls if the display does not answer. Only with
	   FHS_CEC_CAP_EDID. */
	FHS_CEC_REQ_GET_EDID = 0x08,
	/* No data. The device acknowledges, then resets into the ROM USB DFU
	   bootloader. Answered whatever the recipient, so a host tool can send
	   it to the device while the driver holds the interface. Only with
	   FHS_CEC_CAP_DFU. */
	FHS_CEC_REQ_ENTER_DFU = 0x09,
};

enum fhs_cec_cap {
	FHS_CEC_CAP_MONITOR_ALL = 1u << 0,
	FHS_CEC_CAP_EDID = 1u << 1,
	FHS_CEC_CAP_DFU = 1u << 2,
};

enum fhs_cec_event_type {
	FHS_CEC_EV_TX_DONE = 0x01,
	FHS_CEC_EV_RX = 0x02,
};

enum fhs_cec_tx_status {
	FHS_CEC_TX_OK = 0x00,
	FHS_CEC_TX_NACK = 0x01,
	FHS_CEC_TX_ARB_LOST = 0x02,
	FHS_CEC_TX_LOW_DRIVE = 0x03,
	FHS_CEC_TX_ERROR = 0x04,
};

struct fhs_cec_info {
	uint8_t proto_major;
	uint8_t proto_minor;
	uint8_t fw_major;
	uint8_t fw_minor;
	uint8_t fw_patch;
	uint8_t reserved;
	uint16_t phys_addr;
	uint32_t caps;
} __attribute__((packed));

struct fhs_cec_status {
	uint32_t rx_frames;
	uint32_t rx_errors;
	uint32_t rx_dropped;
	uint32_t tx_frames;
	uint32_t tx_nack;
	uint32_t tx_arb_lost;
	uint32_t tx_low_drive;
	uint32_t tx_error;
	uint32_t events_dropped;
	uint32_t recoveries;
} __attribute__((packed));

struct fhs_cec_ev_tx_done {
	uint8_t type;
	uint8_t seq;
	uint8_t status;
} __attribute__((packed));

struct fhs_cec_ev_rx {
	uint8_t type;
	uint8_t len;
	uint8_t frame[FHS_CEC_FRAME_MAX];
} __attribute__((packed));

#endif
