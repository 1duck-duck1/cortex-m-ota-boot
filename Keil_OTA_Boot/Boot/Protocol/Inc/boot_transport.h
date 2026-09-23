/**
 ******************************************************************************
 * @file    boot_transport.h
 * @brief   Boot 侧串口通道：USART2 阻塞收发
 *
 * 升级过程是"一问一答"的：Boot 只有在收到请求后才需要回一个字，收包与写 Flash
 * 又都是阻塞的。既然任何时刻都只有一件事在做，用 DMA + 中断去换那点吞吐毫无
 * 意义，反而要引入缓冲、指针同步与中断优先级问题。所以 Boot 侧一律用 HAL 轮询。
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
 * @brief  初始化 USART2（PD5 / PD6，115200 8N1）为阻塞收发
 * @return true 表示串口可用
 * @note   进入升级模式前调用。初始化失败的后果不是"少个调试口"，而是整条升级
 *         链路不可用，因此调用方必须检查返回值。
 */
bool boot_transport_init(void);

/**
 * @brief  关闭 USART2 并还原引脚
 * @note   跳转前必须调用：串口与它的 GPIO 属于 Boot 的世界，带进 App 会与
 *         App 自己的初始化打架。
 */
void boot_transport_deinit(void);

/**
 * @brief  非阻塞读一个字节
 * @param  byte 读到的字节，不可为 NULL
 * @return true 表示确实读到了一个字节
 */
bool boot_transport_read(uint8_t *byte);

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
