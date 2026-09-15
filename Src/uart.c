#include "uart.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

#define LINE_MAX 64
#define RX_RING  128            /* power of two */

static UART_HandleTypeDef huart2;

/* The ISR only parks the byte. Echo and line editing happen in uart_take_line(),
   because a blocking echo inside the ISR stalls long enough to drop the next byte. */
static volatile uint8_t ring[RX_RING];
static volatile uint16_t head, tail;
static uint8_t rx_byte;

static char line[LINE_MAX];
static uint16_t len;

void uart_init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }

    /* printf must not sit in a libc buffer waiting for a newline. */
    setvbuf(stdout, NULL, _IONBF, 0);

    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}

static int ring_pop(void)
{
    uint8_t c;

    if (head == tail) {
        return -1;
    }
    c = ring[tail];
    tail = (uint16_t)((tail + 1U) % RX_RING);
    return c;
}

char *uart_take_line(void)
{
    int ch;

    while ((ch = ring_pop()) >= 0) {
        char c = (char)ch;

        if (c == '\r' || c == '\n') {
            __io_putchar('\n');
            line[len] = '\0';
            len = 0;
            return line;
        }
        if (c == '\b' || c == 0x7F) {
            if (len > 0U) {
                len--;
                printf("\b \b");
            }
        } else if (len < (LINE_MAX - 1U) && c >= ' ' && c < 0x7F) {
            line[len++] = c;
            __io_putchar(c);          /* local echo, terminal is in raw mode */
        }
    }
    return NULL;
}

/* --- stdio glue. syscall.c routes _write/_read through these. --- */

int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;

    if (ch == '\n') {
        uint8_t cr = '\r';
        HAL_UART_Transmit(&huart2, &cr, 1, HAL_MAX_DELAY);
    }
    HAL_UART_Transmit(&huart2, &c, 1, HAL_MAX_DELAY);
    return ch;
}

int __io_getchar(void)
{
    int c;

    while ((c = ring_pop()) < 0) {
    }
    return c;
}

/* --- interrupt path, must not block --- */

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t next;

    if (huart->Instance != USART2) {
        return;
    }

    next = (uint16_t)((head + 1U) % RX_RING);
    if (next != tail) {               /* full ring: drop, never overwrite */
        ring[head] = rx_byte;
        head = next;
    }

    HAL_UART_Receive_IT(huart, &rx_byte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    /* Overrun/framing garbage on connect: drop it and keep listening. */
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    HAL_UART_Receive_IT(huart, &rx_byte, 1);
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio = {0};

    if (huart->Instance != USART2) {
        return;
    }

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA2 = USART2_TX, PA3 = USART2_RX, both AF1 */
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF1_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}
