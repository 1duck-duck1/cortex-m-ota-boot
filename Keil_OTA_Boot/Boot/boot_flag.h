/**
 ******************************************************************************
 * @file    boot_flag.h
 * @brief   Boot 与 App 之间的跨软件复位通信：.noinit SRAM 邮箱
 ******************************************************************************
 */
#ifndef BOOT_FLAG_H
#define BOOT_FLAG_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  校验并初始化 SRAM 邮箱
 */
void     boot_flag_init(void);

/** @brief App 是否请求进入 Bootloader（F-03） */
bool     boot_flag_request_pending(void);

/** @brief 消费回跳请求标志 */
void     boot_flag_request_clear(void);

/** @brief 连续启动计数 +1（跳转 App 前调用，架构文档 §7.4） */
void     boot_flag_attempts_increment(void);

/** @brief 读取连续启动计数 */
uint32_t boot_flag_attempts_get(void);

/** @brief 清零连续启动计数（App 确认后由 Boot 调用） */
void     boot_flag_attempts_clear(void);

/** @brief App 是否已确认固件健康（F-15 雏形） */
bool     boot_flag_confirm_pending(void);

/** @brief 消费确认标志 */
void     boot_flag_confirm_clear(void);

/** @brief App 写入进入 Bootloader 请求 */
void     boot_flag_request_set(void);

/** @brief App 写入固件确认标志 */
void     boot_flag_confirm_set(void);

/** @brief App 心跳计数（仅调试观察） */
void     boot_flag_heartbeat_increment(void);

#endif /* BOOT_FLAG_H */
