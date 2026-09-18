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

#define BOOT_META_A_ADDR        0x08008000u  /* 元数据副本 A：sector 2 */
#define BOOT_META_B_ADDR        0x0800C000u  /* 元数据副本 B：sector 3 */
#define BOOT_META_COPY_SIZE     (16u * 1024u)
#define BOOT_META_ADDR          BOOT_META_A_ADDR
#define BOOT_META_SIZE          (32u * 1024u)

#define BOOT_DATA_ADDR          0x08010000u  /* 参数数据区：sector 4，NVS 预留 */
#define BOOT_DATA_SIZE          (64u * 1024u)

#define BOOT_SLOT_A_ADDR        0x08020000u  /* App 槽 A：sector 5-7 */
#define BOOT_SLOT_A_SIZE        (384u * 1024u)

#define BOOT_SLOT_B_ADDR        0x08080000u  /* App 槽 B：sector 8-10 */
#define BOOT_SLOT_B_SIZE        (384u * 1024u)

/* Sector 11 必须整块保留；F407 不支持只擦除 sector 的一部分。 */
#define BOOT_RESERVED_ADDR      0x080E0000u
#define BOOT_RESERVED_SIZE      (128u * 1024u)
#define BOOT_MAX_IMAGE_SIZE     BOOT_SLOT_B_SIZE   /* 两槽等容，上限统一 384 KB */

/* SRAM 范围（112 KB 主 SRAM + 16 KB SRAM2，用于 App 栈指针合法性校验）        */
#define BOOT_RAM_BASE           0x20000000u
#define BOOT_RAM_END            0x20020000u

/* -------------------------------------------------------------------------- */
/* .noinit SRAM 邮箱。链接配置必须保留末尾 32 字节，禁止分配普通变量。          */
/* -------------------------------------------------------------------------- */
#define BOOT_MAILBOX_ADDR       0x2001FFE0u
#define BOOT_MAILBOX_SIZE       32u
#define BOOT_MAILBOX_MAGIC      0x424F4F54u  /* 'BOOT' */
#define BOOT_MAGIC_REQUEST      0x52455142u  /* 'REQB' */
#define BOOT_MAGIC_CONFIRM      0x434F4E46u  /* 'CONF' */

/* 镜像与元数据格式。 */
#define BOOT_IMAGE_MAGIC        0x494D4742u  /* 小端字节序：'BGMI' */
#define BOOT_IMAGE_HEADER_VERSION  1u
#define BOOT_METADATA_MAGIC     0x4154454Du  /* 小端字节序：'META' */
#define BOOT_METADATA_VERSION   1u
#define BOOT_HARDWARE_ID        0xF4070001u

/* -------------------------------------------------------------------------- */
/* 行为阈值                                                                    */
/* -------------------------------------------------------------------------- */
#define BOOT_MAX_ATTEMPTS       3u           /* 连续启动达到此次数即拒跳（F-04） */
#define BOOT_HOST_WAIT_MS       500u
#define BOOT_UPDATE_TIMEOUT_MS  10000u
#define BOOT_PROTOCOL_MAX_DATA  1024u

/* -------------------------------------------------------------------------- */
/* 版本                                                                        */
/* -------------------------------------------------------------------------- */
#define BOOT_VERSION_STRING     "0.2.0-dev"

#endif /* BOOT_CONF_H */
