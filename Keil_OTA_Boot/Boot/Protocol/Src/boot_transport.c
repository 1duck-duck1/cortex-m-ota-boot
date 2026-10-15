/**
 ******************************************************************************
 * @file    boot_transport.c
 * @brief   Boot 侧串口通道的实现：RX 中断+环形缓冲，TX 保持阻塞
 ******************************************************************************
 */
#include "boot_transport.h"

#include "stm32f4xx_hal.h"

#include <string.h>

/** RX 环形缓冲长度，单位字节。1 KB 长帧的收尾停顿期间要能兜住后续字节 */
#define BOOT_TRANSPORT_RX_BUFFER_SIZE   (512u)

/** USART2 中断优先级，数值越小越高。低于 SysTick 即可，不与被抢占的轮询逻辑耦合 */
#define BOOT_TRANSPORT_RX_IRQ_PRIORITY  (1u)

/** 单次阻塞写的固定余量，单位毫秒。用于覆盖首字节延迟与调度抖动 */
#define BOOT_TRANSPORT_TX_TIMEOUT_MS   (10u)

/** 每字节的超时预算，单位毫秒。115200 下 1 字节约 0.087 ms，取 1 ms 有十倍余量 */
#define BOOT_TRANSPORT_TX_PER_BYTE_MS  (1u)

static UART_HandleTypeDef s_uart;

/** 串口是否已初始化，避免重复初始化时把引脚时钟再配一遍 */
static bool s_uart_ready = false;

/** RX 环形缓冲。单生产者（中断只动 head）单消费者（主循环只动 tail） */
static uint8_t s_rx_buffer[BOOT_TRANSPORT_RX_BUFFER_SIZE];

/** 写入位置，只在 USART2 中断里推进 */
static volatile uint16_t s_rx_head = 0u;

/** 读取位置，只在主循环里推进 */
static volatile uint16_t s_rx_tail = 0u;

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

/**
 * @brief  把一个字节压进 RX 环形缓冲
 * @param  byte 待入队的字节
 * @note   只在 USART2 中断上下文调用。缓冲满时丢弃最新字节——升级链路里宁可丢
 *         一帧触发重传，也不能覆盖尚未被主循环取走的旧数据。
 */
static void boot_transport_rx_push(uint8_t byte)
{
    uint16_t next = (uint16_t)((s_rx_head + 1u) % BOOT_TRANSPORT_RX_BUFFER_SIZE);

    if (next == s_rx_tail) {
        return;
    }

    s_rx_buffer[s_rx_head] = byte;
    s_rx_head = next;
}

bool boot_transport_init(void)
{
    if (s_uart_ready) {
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

    if (HAL_UART_Init(&s_uart) != HAL_OK) {
        boot_transport_gpio_deinit();
        return false;
    }

    s_rx_head = 0u;
    s_rx_tail = 0u;

    /* 自己开 RXNE 中断，不走 HAL_UART_Receive_IT：Boot 只有 32 KB，HAL 的
       HAL_UART_IRQHandler 连同它引用的回调墙要近 1 KB Rom，在验签已经吃满
       预算的前提下放不下。直接收寄存器只多出几十字节，且行为完全可控。 */
    HAL_NVIC_SetPriority(USART2_IRQn, BOOT_TRANSPORT_RX_IRQ_PRIORITY, 0u);
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    __HAL_UART_ENABLE_IT(&s_uart, UART_IT_RXNE);

    s_uart_ready = true;

    return true;
}

void boot_transport_deinit(void)
{
    if (!s_uart_ready) {
        return;
    }

    __HAL_UART_DISABLE_IT(&s_uart, UART_IT_RXNE);
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);

    (void)HAL_UART_DeInit(&s_uart);
    boot_transport_gpio_deinit();

    s_rx_head = 0u;
    s_rx_tail = 0u;

    s_uart_ready = false;
}

bool boot_transport_read(uint8_t *byte)
{
    if (!s_uart_ready || (byte == NULL) || (s_rx_tail == s_rx_head)) {
        return false;
    }

    *byte = s_rx_buffer[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) % BOOT_TRANSPORT_RX_BUFFER_SIZE);

    return true;
}

/**
 * @brief  USART2 中断服务入口
 * @note   直接操作寄存器，不经过 HAL_UART_IRQHandler。F4 上"先读 SR 再读 DR"
 *         会一次性清掉 RXNE 与 ORE/FE/PE/NE，因此这里读一次 SR 判状态、读一次
 *         DR 取字节并清标志即可。ORE 即使发生也只会丢掉当帧、由上层 CRC 兜底。
 */
void boot_transport_irq_handler(void)
{
    uint32_t sr = USART2->SR;

    if ((sr & USART_SR_RXNE) != 0u) {
        boot_transport_rx_push((uint8_t)(USART2->DR & 0xFFu));
    } else if ((sr & (USART_SR_ORE | USART_SR_FE | USART_SR_PE | USART_SR_NE)) != 0u) {
        /* 只有错误没有数据：读一次 DR 清标志，否则中断会一直挂着重进 */
        (void)USART2->DR;
    }
}

uint32_t boot_transport_write(const uint8_t *data, uint32_t size)
{
    uint32_t offset = 0u;
    uint32_t chunk;
    uint32_t timeout;

    if (!s_uart_ready || (data == NULL) || (size == 0u)) {
        return 0u;
    }

    /* 分段发送：HAL_UART_Transmit 的长度参数是 uint16_t，一次发不了 64 KB。
       超时必须随长度放大——固定 10 ms 只够发 110 个字节，长应答行会被截断。 */
    while (offset < size) {
        chunk = size - offset;
        if (chunk > 1024u) {
            chunk = 1024u;
        }

        timeout = (chunk * BOOT_TRANSPORT_TX_PER_BYTE_MS) + BOOT_TRANSPORT_TX_TIMEOUT_MS;

        if (HAL_UART_Transmit(&s_uart,
                              (uint8_t *)(data + offset),
                              (uint16_t)chunk,
                              timeout) != HAL_OK) {
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
    if (text == NULL) {
        return;
    }

    (void)boot_transport_write((const uint8_t *)text, (uint32_t)strlen(text));
}
