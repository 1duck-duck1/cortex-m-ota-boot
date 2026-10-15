/**
 ******************************************************************************
 * @file    app_transport.c
 * @brief   App 侧串口字节流：循环 DMA 接收 + IDLE 中断 + 轮询取字节
 *
 * 计数器设计：DMA 硬件的写指针会在缓冲里回绕，直接比较它就躲不开"恰好绕了
 * 一整圈"的歧义。因此维护一对单调递增的计数器——生产者 s_rx_total 与消费者
 * s_rx_read，缓冲下标 = 计数 & (SIZE-1)。每次采样把 DMA 的位置增量累加进
 * s_rx_total，回绕由掩码自然处理。
 ******************************************************************************
 */
#include "app_transport.h"

#include "app_config.h"
#include "usart.h"

#include "stm32f4xx_hal.h"

#include <stddef.h>

static uint8_t s_rx_buffer[APP_TRANSPORT_RX_SIZE];
static uint32_t s_rx_read;      /**< 消费者累计字节数（单调递增） */
static uint32_t s_rx_total;     /**< 生产者累计字节数（单调递增） */
static uint32_t s_rx_hw_pos;    /**< 上一次采样到的 DMA 写位置（0 ~ SIZE-1） */
static bool s_rx_overrun;
static bool s_is_ready;

/**
 * @brief  取 USART2 的接收 DMA 句柄
 * @note   CubeMX 把句柄定义在 usart.c 里，且只在 stm32f4xx_it.c 中给了 extern
 *         声明（usart.h 未导出），因此这里通过 usart.c 已建立的 __HAL_LINKDMA
 *         链接关系取用，不再另抄一份 extern——抄一份就等于多一处需要跟着再生成
 *         同步的声明。
 */
static DMA_HandleTypeDef *app_transport_rx_dma(void)
{
    return huart2.hdmarx;
}

/**
 * @brief  把 DMA 当前写位置折算成累计字节数
 * @note   只允许主循环调用（单写者）。IDLE 中断刻意不调用它：中断与主循环
 *         同时做"读位置 → 累加计数"的读改写会丢增量，而 IDLE 只是通知
 *         "一帧结束"，字节最终都会被主循环的下一次采样取到。
 */
static void app_transport_sync(void)
{
    uint32_t position;
    uint32_t delta;

    position = APP_TRANSPORT_RX_SIZE - __HAL_DMA_GET_COUNTER(app_transport_rx_dma());
    if (position >= APP_TRANSPORT_RX_SIZE) {
        /* NDTR 读到 SIZE（尚未收到任何字节）与读到 0（刚绕完一圈）是同一个
           缓冲位置，统一折算成 0，避免位置越界。 */
        position = 0u;
    }

    delta = (position - s_rx_hw_pos) & (APP_TRANSPORT_RX_SIZE - 1u);
    s_rx_hw_pos = position;
    s_rx_total += delta;

    if ((s_rx_total - s_rx_read) > APP_TRANSPORT_RX_SIZE) {
        /* 消费者落后超过一整圈，落后的数据已被 DMA 覆盖。丢弃未取走的字节
           并置位：拿被覆盖过的数据去算 CRC 只会得到更难定位的失败。 */
        s_rx_read = s_rx_total;
        s_rx_overrun = true;
    }
}

void app_transport_init(void)
{
    if (s_is_ready) {
        return;
    }

    s_rx_read = 0u;
    s_rx_total = 0u;
    s_rx_hw_pos = 0u;
    s_rx_overrun = false;

    /* CubeMX 生成的 USART2 RX DMA 是 DMA_NORMAL：收满一次就停，必须反复重启
       并回填长度，收包期间会出现"来不及重启就丢字节"的窗口。这里改成
       CIRCULAR。usart.c 是生成文件，模式只在 App 初始化里覆盖，不改生成代码。 */
    app_transport_rx_dma()->Init.Mode = DMA_CIRCULAR;
    if (HAL_DMA_Init(app_transport_rx_dma()) != HAL_OK) {
        return;
    }

    /* 直接用 HAL_DMA_Start + 置 DMAR 位，不调 HAL_UART_Receive_DMA()：
       HAL 的接收 API 会把 RxState 置为 BUSY_RX 并依赖 TC/IDLE 回调推进状态，
       而本模块只按 DMA 写指针取字节；保持 RxState 为 READY，也能让阻塞发送
       与接收共用同一个句柄而不互相干扰。 */
    if (HAL_DMA_Start(app_transport_rx_dma(),
                      (uint32_t)(uintptr_t)&USART2->DR,
                      (uint32_t)(uintptr_t)s_rx_buffer,
                      APP_TRANSPORT_RX_SIZE) != HAL_OK) {
        return;
    }

    SET_BIT(USART2->CR3, USART_CR3_DMAR);

    /* 循环 DMA 要收满一圈（115200 下约 355 ms）才更新计数，靠 IDLE 中断才能
       在一帧结束时立刻知道线上安静了。处理函数只清标志，见其实现。 */
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);

    s_is_ready = true;
}

void app_transport_deinit(void)
{
    if (s_is_ready) {
        (void)HAL_DMA_Abort(app_transport_rx_dma());
        CLEAR_BIT(USART2->CR3, USART_CR3_DMAR);
        __HAL_UART_DISABLE_IT(&huart2, UART_IT_IDLE);
        (void)HAL_UART_DeInit(&huart2);
        s_is_ready = false;
    }

    /* 复位/回跳前必须不留残留状态（AGENTS.md §11）：DMA 与 USART2 的中断使能、
       挂起标志若留给下一份固件，首帧收发可能直接挂死。 */
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA1_Stream6_IRQn);
}

uint32_t app_transport_read(uint8_t *buffer, uint32_t size)
{
    uint32_t count = 0u;

    if (!s_is_ready || (buffer == NULL) || (size == 0u)) {
        return 0u;
    }

    app_transport_sync();

    while ((count < size) && (s_rx_read != s_rx_total)) {
        buffer[count] = s_rx_buffer[s_rx_read & (APP_TRANSPORT_RX_SIZE - 1u)];
        s_rx_read++;
        count++;
    }

    return count;
}

bool app_transport_write(const uint8_t *data, uint32_t size)
{
    if (!s_is_ready || (data == NULL)) {
        return false;
    }
    if (size == 0u) {
        return true;
    }
    if (size > 0xFFFFu) {
        /* HAL 的长度参数是 uint16_t，超长说明调用方逻辑有误而不是链路问题。 */
        return false;
    }

    /* HAL_UART_Transmit() 的形参不是 const，强制转换只影响本工程内部调用；
       返回时 HAL 已等到 TC，对端确实收到了这一帧。 */
    return HAL_UART_Transmit(&huart2, (uint8_t *)(uintptr_t)data, (uint16_t)size,
                             APP_TRANSPORT_TX_TIMEOUT_MS) == HAL_OK;
}

bool app_transport_is_ready(void)
{
    return s_is_ready;
}

void app_transport_irq_handler(void)
{
    if (!s_is_ready) {
        return;
    }

    /* 先清 IDLE 再让 HAL_UART_IRQHandler() 跑（调用点在 USER CODE 0 区）：
       F4 的 HAL 会把残留的 IDLE 事件当成一次接收完成并回调 RxEventCallback，
       而本模块的接收不归 HAL 管，清干净它才会保持沉默。 */
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_IDLE) != RESET) {
        __HAL_UART_CLEAR_IDLEFLAG(&huart2);
    }

    /* 溢出：F4 的清法是"读 SR 再读 DR"。不清的话后续字节会被持续丢弃，
       状态位一直挂着。溢出意味着有字节永久丢失，置位供 `info` 观察。 */
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE) != RESET) {
        __HAL_UART_CLEAR_OREFLAG(&huart2);
        s_rx_overrun = true;
    }
}

bool app_transport_has_overrun(void)
{
    return s_rx_overrun;
}
