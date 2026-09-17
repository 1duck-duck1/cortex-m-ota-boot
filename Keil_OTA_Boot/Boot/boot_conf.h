/**
 ******************************************************************************
 * @file    boot_conf.h
 * @brief   Bootloader 编译期配置（单一事实来源）
 *
 * 分区地址与 docs/01-architecture.md §5.2 一致，改动前先改文档再改此处。
 ******************************************************************************
 */
#ifndef BOOT_CONF_H
#define BOOT_CONF_H

/* -------------------------------------------------------------------------- */
/* Flash 分区表（STM32F407VGT6，1 MB，12 个大小不均的 sector）                  */
/* -------------------------------------------------------------------------- */
#define BOOT_FLASH_BASE         0x08000000u  /* sector 0 起点                  */

#define BOOT_ADDR               BOOT_FLASH_BASE        /* Bootloader：sector 0-1 */
#define BOOT_SIZE               (32u  * 1024u)

#define BOOT_META_ADDR          0x08008000u  /* 元数据双副本：sector 2-3（v0.3.0 启用） */
#define BOOT_META_SIZE          (32u  * 1024u)

#define BOOT_SLOT_A_ADDR        0x08010000u  /* App 槽 A：sector 4-7           */
#define BOOT_SLOT_A_SIZE        (448u * 1024u)

#define BOOT_SLOT_B_ADDR        0x08080000u  /* App 槽 B：sector 8-11（v0.3.0 启用） */
#define BOOT_SLOT_B_SIZE        (448u * 1024u)

/* SRAM 范围（112 KB 主 SRAM + 16 KB SRAM2，用于 App 栈指针合法性校验）        */
#define BOOT_RAM_BASE           0x20000000u
#define BOOT_RAM_END            0x20020000u

/* -------------------------------------------------------------------------- */
/* RTC 备份寄存器分配（跨复位保持，仅备份域复位才清除；不受 RTC->WPR 保护，    */
/* 只受 PWR->CR.DBP 保护）                                                     */
/* -------------------------------------------------------------------------- */
#define BOOT_BKP_REQUEST        RTC->BKP0R   /* App 请求进入 Bootloader 的魔数  */
#define BOOT_BKP_ATTEMPTS       RTC->BKP1R   /* 连续启动尝试计数               */
#define BOOT_BKP_CONFIRM        RTC->BKP2R   /* App 确认新固件健康的魔数       */
#define BOOT_BKP_HEARTBEAT      RTC->BKP3R   /* App 心跳计数（调试观察用）     */

#define BOOT_MAGIC_REQUEST      0x52455142u  /* 'REQB'：请求进入 Bootloader    */
#define BOOT_MAGIC_CONFIRM      0x434F4E46u  /* 'CONF'：固件确认               */

/* -------------------------------------------------------------------------- */
/* 行为阈值                                                                    */
/* -------------------------------------------------------------------------- */
#define BOOT_MAX_ATTEMPTS       3u           /* 连续启动达到此次数即拒跳（F-04） */

/* -------------------------------------------------------------------------- */
/* 版本                                                                        */
/* -------------------------------------------------------------------------- */
#define BOOT_VERSION_STRING     "0.1.0"

#endif /* BOOT_CONF_H */
