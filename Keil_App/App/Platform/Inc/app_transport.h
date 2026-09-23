/**
 ******************************************************************************
 * @file    app_transport.h
 * @brief   App 侧串口字节流（USART2 + DMA + IDLE 环形缓冲）——常规通道的硬件出口
 *
 * 与 Boot 侧 boot_transport 的差别：Boot 停留在升级模式时没有别的实时任务，用
 * HAL 轮询最可预测；App 还要跑业务，收包只能靠 DMA 搬到内存，CPU 按需取走。
 * 两侧共用同一个物理串口（PD5/PD6）、同一个波特率与同一套 YMODEM 解码器，
 * 差异全部吸收在本模块的"喂字节 / 取字节"接口上。
 *
 * 接收不使用 HAL_UART_Receive_DMA()：HAL 的接收 API 依赖 TC/IDLE 回调来推进
 * 自己的状态机，并把 RxState 置为 BUSY_RX，而本模块的取字节路径是"轮询快照
 * DMA 写指针"，不需要也不希望 HAL 介入。因此这里直接配置循环模式 DMA 并置
 * USART2 的 DMAR 位，让 huart2 保持 READY，阻塞发送可以随时使用它。
 ******************************************************************************
 */
#ifndef APP_TRANSPORT_H
#define APP_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

/** 波特率必须与 Boot 侧 boot_transport 一致，否则命令行与 YMODEM 都建立不起来 */
#define APP_TRANSPORT_BAUDRATE      115200u
/** 单次阻塞发送超时；一行响应不足 200 字节，异常时靠它退出而不是卡死 */
#define APP_TRANSPORT_TX_TIMEOUT_MS 10u
/** 接收环形缓冲大小，必须是 2 的幂（写指针回绕靠掩码取模） */
#define APP_TRANSPORT_RX_SIZE       4096u

/**
 * @brief  初始化 USART2 的循环 DMA 接收与 IDLE 中断
 * @note   必须在 MX_DMA_Init() 与 MX_USART2_UART_Init() 之后调用；重复调用
 *         直接返回。把 CubeMX 生成的 DMA_NORMAL 覆盖成 DMA_CIRCULAR 是刻意的
 *         （理由见文件头），再生成工程不会影响这里。
 *         任一步失败即放弃并保持未就绪：调用方通过 app_transport_is_ready()
 *         判断，串口不可用时其余模块自然退化为只跑业务。
 */
void app_transport_init(void);

/**
 * @brief  停止 DMA 与 USART2，并清除可能残留的中断挂起状态
 * @note   复位前调用：`reset` / `boot` 命令发完最后一行响应后即执行，
 *         避免复位瞬间线上留下半帧；与 Boot 侧"Boot 用过的外设不得留给 App"
 *         是同一条原则的对称形态。
 */
void app_transport_deinit(void);

/**
 * @brief  非阻塞读取已到达的字节
 * @param  buffer 目标缓冲区
 * @param  size   最多读取的字节数
 * @return 实际读取的字节数，无数据时为 0
 * @note   只允许主循环调用：内部会采样 DMA 写指针并推进字节计数，中断里同时
 *         改这两个计数会丢增量。收到多少取多少，绝不等待。
 */
uint32_t app_transport_read(uint8_t *buffer, uint32_t size);

/**
 * @brief  阻塞发送一段数据
 * @param  data 待发送数据
 * @param  size 字节数
 * @return true 表示全部发出
 * @note   命令响应与 YMODEM 控制字节依赖"返回即已上线"的语义（`reset` 命令
 *         发完立刻复位），所以这里用 HAL 轮询发送而不是 DMA。
 */
bool app_transport_write(const uint8_t *data, uint32_t size);

/** @brief 串口是否已初始化可用 */
bool app_transport_is_ready(void);

/**
 * @brief  USART2 中断服务入口，在生成的 USART2_IRQHandler 的 USER CODE 0 区调用
 * @note   只清 IDLE 与 ORE 标志，不改字节计数（见 app_transport_read 的说明）。
 *         必须在 HAL_UART_IRQHandler() 之前调用，否则 HAL 会把残留的 IDLE
 *         当成一次接收完成并回调它自己的弱符号处理函数。
 */
void app_transport_irq_handler(void);

/**
 * @brief  接收缓冲是否发生过覆盖（消费者落后超过一整圈）
 * @return true 表示本次运行中至少丢过一次数据
 * @note   覆盖掉的字节无法还原，上层只能靠协议超时/重传退出；这里把状态暴露
 *         给 `info` 命令，避免"偶发丢字节"变成无从下手的悬案。
 */
bool app_transport_has_overrun(void);

#endif /* APP_TRANSPORT_H */
