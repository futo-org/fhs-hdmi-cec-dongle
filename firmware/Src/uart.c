#include "uart.h"
#include "board.h"
#include "main.h"

#include <stdio.h>

#define RX_RING  128                /* power of two */
#define TX_CHUNK 64

static UART_HandleTypeDef console;

/* ISR only parks the byte; anything blocking in there costs a character time
   at 115200 and the next byte is lost. SPSC: ISR owns head, main owns tail. */
static volatile uint8_t ring[RX_RING];
static volatile uint16_t head;
static volatile uint16_t tail;
static uint8_t rx_byte;

static void console_msp_init(void);

void uart_init(void)
{
    console.Instance = BOARD_CONSOLE_UART;
    console.Init.BaudRate = BOARD_CONSOLE_BAUD;
    console.Init.WordLength = UART_WORDLENGTH_8B;
    console.Init.StopBits = UART_STOPBITS_1;
    console.Init.Parity = UART_PARITY_NONE;
    console.Init.Mode = UART_MODE_TX_RX;
    console.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    console.Init.OverSampling = UART_OVERSAMPLING_16;
    console.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    console.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    console_msp_init();
    if (HAL_UART_Init(&console) != HAL_OK) {
        Error_Handler();
    }

    setvbuf(stdout, NULL, _IONBF, 0);    /* no libc buffering, printf goes now */
    HAL_UART_Receive_IT(&console, &rx_byte, 1);
}

int uart_getc(void)
{
    uint8_t c;

    if (head == tail) {
        return -1;
    }
    c = ring[tail];
    tail = (uint16_t)((tail + 1U) % RX_RING);
    return c;
}

/* --- stdio glue --- */

/* One HAL call per printf, not per char: fewer, fuller USB packets. */
int _write(int file, char *ptr, int len)
{
    uint8_t tx[TX_CHUNK];
    int n = 0;

    (void)file;

    for (int i = 0; i < len; i++) {
        if (ptr[i] == '\n') {
            tx[n++] = '\r';
        }
        tx[n++] = (uint8_t)ptr[i];

        /* -1: LF can add 2 bytes per pass */
        if (n >= (int)(sizeof(tx) - 1)) {
            HAL_UART_Transmit(&console, tx, (uint16_t)n, HAL_MAX_DELAY);
            n = 0;
        }
    }
    if (n > 0) {
        HAL_UART_Transmit(&console, tx, (uint16_t)n, HAL_MAX_DELAY);
    }
    return len;
}

int __io_putchar(int ch)
{
    char c = (char)ch;

    _write(1, &c, 1);
    return ch;
}

int __io_getchar(void)
{
    int c;

    while ((c = uart_getc()) < 0) {
    }
    return c;
}

/* --- interrupt path, must not block --- */

void uart_irq_handler(void)
{
    HAL_UART_IRQHandler(&console);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t next;

    if (huart->Instance != BOARD_CONSOLE_UART) {
        return;
    }

    next = (uint16_t)((head + 1U) % RX_RING);
    if (next != tail) {             /* full: drop, never overwrite */
        ring[head] = rx_byte;
        head = next;
    }
    HAL_UART_Receive_IT(huart, &rx_byte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != BOARD_CONSOLE_UART) {
        return;
    }

    /* overrun/framing junk on connect: drop, keep listening */
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    HAL_UART_Receive_IT(huart, &rx_byte, 1);
}

/* --- private --- */

static void console_msp_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    BOARD_CONSOLE_CLK_ENABLE();
    BOARD_CONSOLE_PORT_CLK_ENABLE();

    gpio.Pin = BOARD_CONSOLE_PINS;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = BOARD_CONSOLE_AF;
    HAL_GPIO_Init(BOARD_CONSOLE_PORT, &gpio);

    HAL_NVIC_SetPriority(BOARD_CONSOLE_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(BOARD_CONSOLE_IRQn);
}
