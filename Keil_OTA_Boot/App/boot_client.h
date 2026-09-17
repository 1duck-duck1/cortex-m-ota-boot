/**
 ******************************************************************************
 * @file    boot_client.h
 * @brief   App 侧调用 Bootloader 约定接口的最小库（链接进 App，不进 Boot）
 ******************************************************************************
 */
#ifndef BOOT_CLIENT_H
#define BOOT_CLIENT_H

/**
 * @brief  初始化与 Bootloader 的通信通道（备份寄存器）
 */
void boot_client_init(void);

/**
 * @brief  确认新固件健康（F-15 雏形）
 * @note   App 自检通过后调用；Boot 下次上电看到确认即清零启动计数
 */
void boot_client_confirm(void);

/**
 * @brief  请求进入 Bootloader 升级模式（F-03），触发系统复位，不返回
 */
void boot_client_request_update(void);

/**
 * @brief  心跳计数 +1（仅调试观察用，调试器看 BKPR3 递增即 App 活着）
 */
void boot_client_heartbeat(void);

#endif /* BOOT_CLIENT_H */
