/**
 ******************************************************************************
 * @file    boot_transport.c
 * @brief   Boot 侧串口通道的实现
 ******************************************************************************
 */
#include "boot_transport.h"

#include <string.h>

#include "stm32f4xx_hal.h"

/** 单次阻塞读的超时，单位毫秒。轮询本身要立刻返回，这个值只用来兜住硬件异常 */
#define BOOT_TRANSPORT_RX_TIMEOUT_MS   (1u)

/** 单次阻塞写的固定余量，单位毫秒。用于覆盖首字节延迟与调度抖动 */
#define BOOT_TRANSPORT_TX_TIMEOUT_MS   (10u)

/** 每字节的超时预算，单位毫秒。115200 下 1 字节约 0.087 ms，取 1 ms 有十倍余量 */
#define BOOT_TRANSPORT_TX_PER_BYTE_MS  (1u)

static UART_HandleTypeDef s_uart;

/** 串口是否已初始化，避免重复初始化时把引脚时钟再配一遍 */
static bool s_uart_ready = false;

/**
 * @brief  把 USART2 的引脚与时钟配置好
 * @note   Boot 工程没有生成 HAL_UART_MspInit（CubeMX 里没配 USART2），只能在这里
 *         自己做。PD5 = TX，PD6 = RX，复用功能 AF7。
 */
static void boot_transport_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOD, &gpio);
}

/**
 * @brief  还原 USART2 的引脚与时钟
 */
static void boot_transport_gpio_deinit(void)
{
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_5 | GPIO_PIN_6);

    __HAL_RCC_USART2_CLK_DISABLE();
    __HAL_RCC_GPIOD_CLK_DISABLE();
}

bool boot_transport_init(void)
{
    if (s_uart_ready)
    {
        return true;
    }

    boot_transport_gpio_init();

    (void)memset(&s_uart, 0, sizeof(s_uart));
    s_uart.Instance = USART2;
    s_uart.Init.BaudRate = 115200u;
    s_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart.Init.StopBits = UART_STOPBITS_1;
    s_uart.Init.Parity = UART_PARITY_NONE;
    s_uart.Init.Mode = UART_MODE_TX_RX;
    s_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&s_uart) != HAL_OK)
    {
        boot_transport_gpio_deinit();
        return false;
    }

    s_uart_ready = true;

    return true;
}

void boot_transport_deinit(void)
{
    if (!s_uart_ready)
    {
        return;
    }

    (void)HAL_UART_DeInit(&s_uart);
    boot_transport_gpio_deinit();

    s_uart_ready = false;
}

bool boot_transport_read(uint8_t *byte)
{
    if (!s_uart_ready || (byte == NULL))
    {
        return false;
    }

    return (HAL_UART_Receive(&s_uart, byte, 1u, BOOT_TRANSPORT_RX_TIMEOUT_MS) == HAL_OK);
}

uint32_t boot_transport_write(const uint8_t *data, uint32_t size)
{
    uint32_t offset = 0u;
    uint32_t chunk;
    uint32_t timeout;

    if (!s_uart_ready || (data == NULL) || (size == 0u))
    {
        return 0u;
    }

    /* 分段发送：HAL_UART_Transmit 的长度参数是 uint16_t，一次发不了 64 KB。
       超时必须随长度放大——固定 10 ms 只够发 110 个字节，长应答行会被截断。 */
    while (offset < size)
    {
        chunk = size - offset;
        if (chunk > 1024u)
        {
            chunk = 1024u;
        }

        timeout = (chunk * BOOT_TRANSPORT_TX_PER_BYTE_MS) + BOOT_TRANSPORT_TX_TIMEOUT_MS;

        if (HAL_UART_Transmit(&s_uart,
                              (uint8_t *)(data + offset),
                              (uint16_t)chunk,
                              timeout) != HAL_OK)
        {
            break;
        }

        offset += chunk;
    }

    return offset;
}

void boot_transport_write_byte(uint8_t byte)
{
    (void)boot_transport_write(&byte, 1u);
}

void boot_transport_write_str(const char *text)
{
    if (text == NULL)
    {
        return;
    }

    (void)boot_transport_write((const uint8_t *)text, (uint32_t)strlen(text));
}
