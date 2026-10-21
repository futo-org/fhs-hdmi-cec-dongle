#include "ddc.h"
#include "board.h"

#define EDID_I2C_ADDR       (0x50U << 1)
#define EDID_BLOCKS_PER_SEGMENT 2U
#define DDC_TIMEOUT_MS      1000U

/* ~5 kHz from the 8 MHz HSI kernel clock: PRESC 15 (2 us ticks), SCLL = SCLH =
   49 (100 us). Slow enough for the internal ~40k pull-ups to charge a cable. */
#define DDC_TIMING          0xF0423131U

#define EDID_EXT_COUNT_OFFSET   126U
#define CTA_EXT_TAG             0x02U
#define CTA_DTD_OFFSET          2U
#define CTA_DATA_BLOCKS_START   4U
#define CTA_TAG_VENDOR          3U
#define HDMI_VSDB_MIN_LEN       5U

static const uint8_t hdmi_oui[] = { 0x03, 0x0C, 0x00 };    /* 00-0C-03, little endian */

static const struct {
    ddc_result_t result;
    const char *name;
} result_names[] = {
    { DDC_OK,        "ok" },
    { DDC_NO_ACK,    "no ack" },
    { DDC_BUS_ERROR, "bus error" },
};

#define RESULT_COUNT (sizeof(result_names) / sizeof(result_names[0]))

static I2C_HandleTypeDef i2c;

const char *ddc_result_name(ddc_result_t r)
{
    for (size_t i = 0; i < RESULT_COUNT; i++) {
        if (result_names[i].result == r) {
            return result_names[i].name;
        }
    }
    return "?";
}

static void pins_claim(void)
{
    GPIO_InitTypeDef gpio = {0};

    BOARD_DDC_PORT_CLK_ENABLE();
    gpio.Pin = BOARD_DDC_SCL_PIN | BOARD_DDC_SDA_PIN;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = BOARD_DDC_AF;
    HAL_GPIO_Init(BOARD_DDC_PORT, &gpio);
}

static void pins_release(void)
{
    HAL_GPIO_DeInit(BOARD_DDC_PORT, BOARD_DDC_SCL_PIN | BOARD_DDC_SDA_PIN);
}

ddc_result_t ddc_read_edid_block(uint8_t block, uint8_t *out)
{
    HAL_StatusTypeDef status;
    uint32_t error;

    if (block >= EDID_BLOCKS_PER_SEGMENT) {
        return DDC_BUS_ERROR;
    }

    BOARD_DDC_CLK_ENABLE();
    pins_claim();

    i2c.Instance = BOARD_DDC;
    i2c.Init.Timing = DDC_TIMING;
    i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&i2c) != HAL_OK) {
        pins_release();
        BOARD_DDC_CLK_DISABLE();
        return DDC_BUS_ERROR;
    }

    status = HAL_I2C_Mem_Read(&i2c, EDID_I2C_ADDR, (uint16_t)(block * EDID_BLOCK_SIZE),
                              I2C_MEMADD_SIZE_8BIT, out, EDID_BLOCK_SIZE, DDC_TIMEOUT_MS);
    error = HAL_I2C_GetError(&i2c);

    HAL_I2C_DeInit(&i2c);
    pins_release();
    BOARD_DDC_CLK_DISABLE();

    if (status == HAL_OK) {
        return DDC_OK;
    }
    return (error == HAL_I2C_ERROR_AF) ? DDC_NO_ACK : DDC_BUS_ERROR;
}

int edid_checksum_ok(const uint8_t *block)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < EDID_BLOCK_SIZE; i++) {
        sum = (uint8_t)(sum + block[i]);
    }
    return sum == 0U;
}

uint8_t edid_extension_count(const uint8_t *base)
{
    return base[EDID_EXT_COUNT_OFFSET];
}

int edid_phys_addr(const uint8_t *ext, uint16_t *out)
{
    uint8_t end = ext[CTA_DTD_OFFSET];
    uint8_t i = CTA_DATA_BLOCKS_START;

    if (ext[0] != CTA_EXT_TAG || end >= EDID_BLOCK_SIZE) {
        return 0;
    }
    while (i < end) {
        uint8_t tag = ext[i] >> 5;
        uint8_t len = ext[i] & 0x1FU;
        const uint8_t *body = &ext[i + 1U];

        if (i + 1U + len > end) {
            return 0;
        }
        if (tag == CTA_TAG_VENDOR && len >= HDMI_VSDB_MIN_LEN &&
            body[0] == hdmi_oui[0] && body[1] == hdmi_oui[1] && body[2] == hdmi_oui[2]) {
            *out = (uint16_t)((body[3] << 8) | body[4]);
            return 1;
        }
        i = (uint8_t)(i + len + 1U);
    }
    return 0;
}
