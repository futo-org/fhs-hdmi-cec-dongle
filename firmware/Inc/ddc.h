#ifndef DDC_H
#define DDC_H

#include <stdint.h>

#define EDID_BLOCK_SIZE 128U

/* Upper bound on one block read, stuck bus included. A good read takes ~240 ms. */
#define DDC_READ_TIMEOUT_MS 500U

typedef enum {
    DDC_OK,
    DDC_NO_ACK,         /* nothing answered at the EDID address */
    DDC_BUS_ERROR,      /* lost arbitration, stuck line or timeout */
} ddc_result_t;

const char *ddc_result_name(ddc_result_t r);

/* Read one 128-byte EDID block from the display. The pins are only claimed for
   the duration of the read, so the source's own DDC traffic is left alone. */
ddc_result_t ddc_read_edid_block(uint8_t block, uint8_t *out);

/* 1 if the block's bytes sum to 0 mod 256. */
int edid_checksum_ok(const uint8_t *block);

/* Number of extension blocks the base block says follow it. */
uint8_t edid_extension_count(const uint8_t *base);

/* The CEC physical address from a CTA-861 extension's HDMI vendor block.
   0 if the block has none. */
int edid_phys_addr(const uint8_t *ext, uint16_t *out);

#endif
