/**
 ******************************************************************************
 * @file    app_config.h
 * @brief   应用工程的槽位、邮箱和链接地址配置
 ******************************************************************************
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

/** 应用槽位配置 */
#define APP_SLOT_A_BASE       (0x08020000u)
/** Slot B 起始地址 */
#define APP_SLOT_B_BASE       (0x08080000u)
/** 应用槽位大小，单位字节 */
#define APP_SLOT_SIZE         (384u * 1024u)
/** 槽位 Flash 扇区：Slot A 占 sector 5-7，Slot B 占 sector 8-10，各 3 个 */
#define APP_SLOT_SECTOR_COUNT (3u)

/** 参数数据区（NVS）：sector 4，与 Boot 侧 BOOT_DATA_ADDR/BOOT_DATA_SIZE 一致 */
#define APP_DATA_ADDR         (0x08010000u)
/** 参数数据区大小，单位字节 */
#define APP_DATA_SIZE         (64u * 1024u)

/** SRAM 范围（112 KB 主 SRAM + 16 KB SRAM2），用于校验新镜像的初始 MSP */
#define APP_RAM_BASE          (0x20000000u)
#define APP_RAM_END           (0x20020000u)

/** 应用邮箱配置 */
#define APP_MAILBOX_ADDR      (0x2001FFE0u)

#define APP_MAILBOX_MAGIC     (0x424F4F54u)
/** 应用邮箱魔数，ASCII "BOOT" */
#define APP_MAGIC_REQUEST     (0x52455142u)
/** 应用邮箱请求标志，ASCII "REQB" */
#define APP_MAGIC_CONFIRM     (0x434F4E46u)

#define APP_SLOT_ID_A         (0u)
#define APP_SLOT_ID_B         (1u)
#define APP_SLOT_ID_NONE      (0xFFFFFFFFu)
#define APP_MAILBOX_FLAG_PENDING_LAUNCH (1u << 0)

#if defined(APP_SLOT_B)
#define APP_SLOT_BASE         APP_SLOT_B_BASE
#define APP_SLOT_ID           APP_SLOT_ID_B
#else
#define APP_SLOT_BASE         APP_SLOT_A_BASE
#define APP_SLOT_ID           APP_SLOT_ID_A
#endif

/** 应用版本串，供 `info` 命令确认当前运行的是哪份固件 */
#define APP_VERSION_STRING    "0.2.0-dev"

#endif /* APP_CONFIG_H */
