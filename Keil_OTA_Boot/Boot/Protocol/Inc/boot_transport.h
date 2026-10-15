/**
 ******************************************************************************
 * @file    boot_transport.h
 * @brief   Boot 侧串口通道：USART2 RX 中断+环形缓冲，TX 阻塞
 *
 * 升级是"一问一答"的，收包与写 Flash 都是阻塞的，所以发送侧继续保持 HAL 轮询。
 * 但接收侧不能轮询：第 0 包要携带 Ed25519 签名，固定走 STX 的 1024 字节长帧，
 * 而收满整帧算 CRC16、以及随后的擦除都会让主循环停顿上百毫秒。若无缓冲，这段
 * 停顿里到达的字节会被硬件 ORE 直接丢掉，导致长帧半途断掉、状态机再也不推进。
 * 因此 RX 改为"逐字节中断 + 环形缓冲"：字节采集与状态机处理彻底解耦。中断
 * 处理直接操作 USART2 寄存器，不经过 HAL_UART_IRQHandler——后者连同它引用的
 * 回调链要近 1 KB Rom，而 Boot 的 32 KB 已被验签占满，放不下；寄存器版本只多
 * 出几十字节，行为也完全可控。
 *
 * USART2 的硬件初始化在本模块内完成，不依赖 CubeMX：Boot 工程里 USART1 留给
 * 调试输出，USART2 只在进入升级模式时才初始化，正常启动路径根本不碰串口。
 ******************************************************************************
 */
#ifndef BOOT_TRANSPORT_H
#define BOOT_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  初始化 USART2（PD5 / PD6，115200 8N1）：TX 阻塞、RX 中断+环形缓冲
 * @return true 表示串口可用
 * @note   进入升级模式前调用。初始化失败的后果不是"少个调试口"，而是整条升级
 *         链路不可用，因此调用方必须检查返回值。内部会开启 USART2 的 RXNE 中断，
 *         需要工程提供 USART2_IRQHandler 并调用 boot_transport_irq_handler()。
 */
bool boot_transport_init(void);

/**
 * @brief  关闭 USART2 并还原引脚
 * @note   跳转前必须调用：串口与它的 GPIO 属于 Boot 的世界，带进 App 会与
 *         App 自己的初始化打架。内部会关闭并清除 USART2 中断。
 */
void boot_transport_deinit(void);

/**
 * @brief  非阻塞读一个字节
 * @param  byte 读到的字节，不可为 NULL
 * @return true 表示确实读到了一个字节
 * @note   从 RX 环形缓冲出队，缓冲为空时立即返回 false，不阻塞。
 */
bool boot_transport_read(uint8_t *byte);

/**
 * @brief  USART2 中断服务入口
 * @note   由 USART2_IRQHandler 调用。直接读 SR / DR 处理 RXNE 与错误标志，
 *         把收到的字节压进环形缓冲，不依赖 HAL 的中断接收回调机制。
 */
void boot_transport_irq_handler(void);

/**
 * @brief  阻塞发送一段数据
 * @param  data 数据指针
 * @param  size 字节数
 * @return 实际发出的字节数
 * @note   YMODEM 的应答只有一两个字节，阻塞等待的代价可以忽略；但绝不能因为
 *         发送失败就跳过状态机推进，返回值交由调用方忽略即可。
 */
uint32_t boot_transport_write(const uint8_t *data, uint32_t size);

/**
 * @brief  阻塞发送一个字节
 * @param  byte 待发送的字节
 */
void boot_transport_write_byte(uint8_t byte);

/**
 * @brief  阻塞发送一个以 '\0' 结尾的字符串
 * @param  text 字符串，不可为 NULL
 */
void boot_transport_write_str(const char *text);

#endif /* BOOT_TRANSPORT_H */
