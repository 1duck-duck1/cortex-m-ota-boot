/**
 ******************************************************************************
 * @file    boot_flash.h
 * @brief   Boot 侧 Flash 访问：整槽 / Metadata / NVS 的擦除、写入与读出
 *
 * 本模块只提供几何受限的原子操作，不判断"该不该写这个区域"——那是
 * boot_slot_writer 与 boot_metadata 的职责。这里的硬约束只有地址几何：
 * 每个入口都要求调用方给出合法编号或落在区间内的地址，越界一律拒绝，
 * 保证上层即使传错参数也伤不到 Boot 区与别的分区。
 *
 * 擦除与编程函数放在 RAM 中执行（BOOT_RAMCODE 段）：擦写期间取指会与 Flash
 * 访问争用，把这几段代码搬进 SRAM 后，擦写流程不再依赖被操作的对象。
 ******************************************************************************
 */
#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdbool.h>
#include <stdint.h>

/** 片内 Flash 的扇区总数（sector 0-11） */
#define BOOT_FLASH_SECTOR_TOTAL    (12u)

/** 扇区编号非法时的返回值 */
#define BOOT_FLASH_SECTOR_INVALID  (0xFFFFFFFFu)

#if defined(__CC_ARM)
#define BOOT_RAMFUNC   __attribute__((section("BOOT_RAMCODE")))   /* AC5：自命名段，由 scatter 放进 SRAM */
#elif defined(__GNUC__)
#define BOOT_RAMFUNC   __attribute__((section(".boot_ramcode")))  /* GCC：同义的自命名段 */
#else
#define BOOT_RAMFUNC
#endif

/**
 * @brief  取槽位起始地址
 * @param  slot_id BOOT_SLOT_ID_A / BOOT_SLOT_ID_B
 * @return 槽起始地址；slot_id 非法时返回 0
 */
uint32_t boot_flash_slot_base(uint32_t slot_id);

/**
 * @brief  取另一槽的编号
 * @param  slot_id BOOT_SLOT_ID_A / BOOT_SLOT_ID_B
 * @return 另一个槽的编号；slot_id 非法时返回 BOOT_SLOT_ID_NONE
 */
uint32_t boot_flash_other_slot(uint32_t slot_id);

/**
 * @brief  校验一段地址范围是否完整落在某个槽内
 * @param  address 起始地址
 * @param  size    字节数，必须大于 0
 * @return true 表示整段落在 Slot A 或 Slot B 内
 * @note   Boot 区、Metadata 区与 NVS 区都不在任何槽的区间里，
 *         因此天然被这一条挡掉。
 */
bool boot_flash_range_in_slot(uint32_t address, uint32_t size);

/**
 * @brief  校验一段地址范围是否全部处于擦除态
 * @param  address 起始地址
 * @param  size    字节数，必须大于 0
 * @return true 表示每个字节都是 0xFF
 */
bool boot_flash_is_erased(uint32_t address, uint32_t size);

/**
 * @brief  擦除整个槽
 * @param  slot_id BOOT_SLOT_ID_A / BOOT_SLOT_ID_B
 * @return true 表示 3 个扇区全部擦除完成
 * @retval false slot_id 非法或任一扇区擦除失败
 * @note   擦除耗时数百毫秒，期间 CPU 会被 Flash 总线拖住，不能指望串口收到数据。
 */
BOOT_RAMFUNC bool boot_flash_erase_slot(uint32_t slot_id);

/**
 * @brief  擦除整个参数数据区（sector 4，64 KB）
 * @return true 表示擦除完成
 */
BOOT_RAMFUNC bool boot_flash_erase_nvs(void);

/**
 * @brief  写入一段数据到任意 Flash 地址
 * @param  address 目标地址，必须 4 字节对齐且落在某个槽内
 * @param  data    数据指针
 * @param  size    字节数，必须大于 0
 * @return true 表示已落盘
 * @retval false 地址未对齐、越出槽范围，或编程失败
 * @note   不足 4 字节的尾部按字补齐，未覆盖的字节保持 0xFF。
 */
BOOT_RAMFUNC bool boot_flash_write(uint32_t address, const uint8_t *data, uint32_t size);

/**
 * @brief  擦除整个 Metadata 日志扇区（sector 11，128 KB）
 * @return true 表示擦除完成
 * @note   仅在日志写满、需要回收整扇区时调用，耗时为秒级。
 */
BOOT_RAMFUNC bool boot_flash_erase_metadata(void);

/**
 * @brief  向 Metadata 日志区追加一条记录（不擦除）
 * @param  address 目标地址，必须 4 字节对齐且落在 Metadata 区内
 * @param  data    数据指针
 * @param  size    字节数，必须大于 0 且不越过 Metadata 区末尾
 * @return true 表示已落盘
 * @note   调用方必须保证目标位置处于擦除态；本函数不做整扇区擦除，
 *         否则会把日志里更早的记录一起抹掉。
 */
BOOT_RAMFUNC bool boot_flash_write_metadata(uint32_t address,
                                            const void *data,
                                            uint32_t size);

/**
 * @brief  向参数数据区写入
 * @param  offset 相对 BOOT_NVS_ADDR 的偏移，必须 4 字节对齐
 * @param  data   数据指针
 * @param  size   字节数，必须大于 0
 * @return true 表示已落盘
 * @note   只保证"写"能落盘，不保证可重复写：同一地址二次写入前必须先擦除
 *         整个扇区（Flash 只能把 1 写成 0）。
 */
BOOT_RAMFUNC bool boot_flash_write_nvs(uint32_t offset, const uint8_t *data, uint32_t size);

/**
 * @brief  按地址读出数据
 * @param  address 源地址
 * @param  data    目标缓冲区
 * @param  size    字节数，必须大于 0
 * @return true 表示读取完成
 * @note   逐字节用 volatile 读，避免编译器把回读校验优化成常量。
 */
bool boot_flash_read(uint32_t address, uint8_t *data, uint32_t size);

/**
 * @brief  从参数数据区读出
 * @param  offset 相对 BOOT_NVS_ADDR 的偏移
 * @param  data   目标缓冲区
 * @param  size   字节数，必须大于 0
 * @return true 表示读取完成
 * @retval false 参数非法或偏移越出 NVS 区
 */
bool boot_flash_read_nvs(uint32_t offset, uint8_t *data, uint32_t size);

#endif /* BOOT_FLASH_H */
