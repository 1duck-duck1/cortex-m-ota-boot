/**
 ******************************************************************************
 * @file    boot_conf.h
 * @brief   Bootloader 的分区表、魔数、邮箱布局与超时参数
 *
 * 本文件是 Boot 侧唯一的"布局真相"，所有地址、长度、魔数都从这里取。
 * 其中槽位地址、NVS 地址、邮箱地址与 App 侧的 app_config.h 必须逐字节一致：
 * 两个工程各自编译、没有共享源文件，唯一能对齐的就是这些常量。
 ******************************************************************************
 */
#ifndef BOOT_CONF_H
#define BOOT_CONF_H

#include <stdint.h>

/* ------------------------------ Flash 分区表 ------------------------------ */
/* STM32F407VGT6 片内 Flash 1 MB：sector 0-3 各 16 KB，sector 4 为 64 KB，
   sector 5-11 各 128 KB。分区边界必须落在扇区边界上，否则擦除会误伤邻居。 */

#define BOOT_FLASH_BASE            (0x08000000u)              /* 片内 Flash 基址 */
#define BOOT_FLASH_SIZE            (1024u * 1024u)            /* 片内 Flash 总容量 */

#define BOOT_BOOT_ADDR             (0x08000000u)              /* Bootloader 区起始，sector 0 */
#define BOOT_BOOT_SIZE             (32u * 1024u)              /* Bootloader 区长度，sector 0-1 */

#define BOOT_METADATA_A_ADDR       (0x08008000u)              /* Metadata 副本 A，sector 2 */
#define BOOT_METADATA_B_ADDR       (0x0800C000u)              /* Metadata 副本 B，sector 3 */
#define BOOT_METADATA_SIZE         (16u * 1024u)              /* 单个 Metadata 占满一个扇区 */

#define BOOT_NVS_ADDR              (0x08010000u)              /* 参数数据区，sector 4 */
#define BOOT_NVS_SIZE              (64u * 1024u)              /* 参数数据区长度 */

#define BOOT_SLOT_A_ADDR           (0x08020000u)              /* Slot A 起始，sector 5 */
#define BOOT_SLOT_B_ADDR           (0x08080000u)              /* Slot B 起始，sector 8 */
#define BOOT_SLOT_SIZE             (384u * 1024u)             /* 单槽容量，含 0x200 镜像头 */
#define BOOT_SLOT_SECTOR_COUNT     (3u)                       /* 单槽跨 3 个 128 KB 扇区 */

#define BOOT_RESERVED_ADDR         (0x080E0000u)              /* 预留区起始，sector 11 */
#define BOOT_RESERVED_SIZE         (128u * 1024u)             /* 预留区长度 */

/* ------------------------------ 槽位标识 ------------------------------ */
#define BOOT_SLOT_ID_A             (0u)                       /* Slot A 的编号 */
#define BOOT_SLOT_ID_B             (1u)                       /* Slot B 的编号 */
#define BOOT_SLOT_ID_NONE          (0xFFFFFFFFu)              /* 无槽哨兵，用于 active/pending */

/* ------------------------------ SRAM 布局 ------------------------------ */
#define BOOT_RAM_BASE              (0x20000000u)              /* SRAM 起始，向量表 MSP 下限 */
#define BOOT_RAM_END               (0x20020000u)              /* SRAM 结束（不含），MSP 上限 */

#define BOOT_MAILBOX_ADDR          (0x2001FFE0u)              /* 跨复位邮箱基址，SRAM2 顶部 32 字节 */
#define BOOT_MAILBOX_SIZE          (32u)                      /* 邮箱长度，两端 scatter 均已挖掉 */

/* ------------------------------ 魔数与哨兵 ------------------------------ */
#define BOOT_METADATA_MAGIC        (0x4D455441u)              /* ASCII "META" */
#define BOOT_MAILBOX_MAGIC         (0x424F4F54u)              /* ASCII "BOOT"，邮箱有效性标志 */
#define BOOT_MAILBOX_REQUEST       (0x52455142u)              /* ASCII "REQB"，App 请求回 Boot */
#define BOOT_MAILBOX_CONFIRM       (0x434F4E46u)              /* ASCII "CONF"，App 确认试运行成功 */

/** 邮箱标志位：本次启动的镜像是 Pending 试运行镜像 */
#define BOOT_MAILBOX_FLAG_PENDING  (1u << 0u)

/* ------------------------------ 运行参数 ------------------------------ */
#define BOOT_DEFAULT_MAX_ATTEMPTS  (3u)                       /* Pending 镜像默认试运行次数上限 */
#define BOOT_VERSION_STRING        "0.2.0"                    /* Boot 自身版本号，供 info 展示 */

/* ------------------------------ 签名策略 ------------------------------ */
/* 量产置 1：任何进槽/启动的镜像都必须带可被内建公钥验证的 Ed25519 签名。
   开发期置 0：跳过验签，并放行 ota adopt（设备无私钥，无法为 Keil 直烧的槽签名）。 */
#define BOOT_REQUIRE_SIGNATURE     (1u)

/* ------------------------------ 超时参数 ------------------------------ */
#define BOOT_OTA_C_INTERVAL_MS         (1000u)                /* 握手期周期发 'C' 的间隔 */
#define BOOT_OTA_HANDSHAKE_TIMEOUT_MS  (15000u)               /* 等第 0 包的静默超时 */
#define BOOT_OTA_SILENCE_TIMEOUT_MS    (5000u)                /* 收发阶段的静默超时 */
#define BOOT_OTA_STALL_TIMEOUT_MS      (30000u)               /* 持续有字节但迟迟不成帧的上限 */
#define BOOT_OTA_MAX_RETRY             (10u)                  /* 同一包连续重传出错上限 */

/* ------------------------------ 编译期自检 ------------------------------ */
#if (BOOT_MAILBOX_ADDR + BOOT_MAILBOX_SIZE) > BOOT_RAM_END
#error "邮箱越出 SRAM 上界"
#endif

#if (BOOT_SLOT_A_ADDR + BOOT_SLOT_SIZE) > BOOT_SLOT_B_ADDR
#error "Slot A 与 Slot B 重叠"
#endif

#if (BOOT_SLOT_B_ADDR + BOOT_SLOT_SIZE) > BOOT_RESERVED_ADDR
#error "Slot B 越入预留区"
#endif

#endif /* BOOT_CONF_H */
