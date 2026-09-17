/**
 ******************************************************************************
 * @file    boot_flag.h
 * @brief   Boot 与 App 之间的跨复位通信：RTC 备份寄存器标志
 ******************************************************************************
 */
#ifndef BOOT_FLAG_H
#define BOOT_FLAG_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  使能备份寄存器访问（PWR 时钟 + DBP 解锁 + RTC 外设时钟）
 * @note   上电/复位后调用一次，Boot 与 App 侧各调各的
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

#endif /* BOOT_FLAG_H */
