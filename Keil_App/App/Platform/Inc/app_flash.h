/**
 ******************************************************************************
 * @file    app_flash.h
 * @brief   App 侧 Flash 访问：两个 App 槽与参数数据区（NVS）
 *
 * 与 Boot 侧 boot_flash 的分工相同：只提供"扇区擦除 + 按地址写入 + 按地址读出"，
 * 不判断"该不该写这个槽"（那是 app_slot_writer 的职责）。本模块的硬约束只有
 * 地址几何：**只接受 Slot A / Slot B 与 NVS 三个区间**，Boot 区、Metadata 区与
 * 保留区一律拒绝，这样即使上层传错地址也伤不到别的东西。
 *
 * App 与 Boot 共用同一片 Flash，但没有共享代码：两个 Keil 工程各自编译。
 * 因此这里的扇区编号必须与 Boot 侧 boot_flash.c 保持一致（Slot A = sector 5，
 * Slot B = sector 8，NVS = sector 4）。
 ******************************************************************************
 */
#ifndef APP_FLASH_H
#define APP_FLASH_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  取槽位起始地址
 * @param  slot_id APP_SLOT_ID_A / APP_SLOT_ID_B
 * @return 槽起始地址；slot_id 不是 A/B 时返回 0
 */
uint32_t app_flash_slot_base(uint32_t slot_id);

/**
 * @brief  取槽位容量
 * @param  slot_id APP_SLOT_ID_A / APP_SLOT_ID_B
 * @return 容量字节数；slot_id 不是 A/B 时返回 0
 */
uint32_t app_flash_slot_size(uint32_t slot_id);

/**
 * @brief  校验一段地址范围是否落在 Slot A / Slot B 内
 * @param  address 起始地址
 * @param  size    字节数，必须大于 0
 * @return true 表示整段落在某一个槽内
 */
bool app_flash_range_valid(uint32_t address, uint32_t size);

/**
 * @brief  擦除整个槽（3 个 sector，384 KB）
 * @param  slot_id 目标槽，只接受 Slot A / Slot B
 * @return true 表示擦除完成
 * @retval false slot_id 非法，或 HAL 报扇区错误
 * @note   调用方必须先确认这是非运行槽。擦 3 个 sector 期间 CPU 会被 Flash 总线
 *         stall 数百毫秒以上，期间不能指望串口收到任何字节。
 */
bool app_flash_erase_slot(uint32_t slot_id);

/**
 * @brief  按地址写入一段数据
 * @param  address 目标地址，必须 4 字节对齐且整段落在某个槽内
 * @param  data    数据指针
 * @param  size    字节数，必须大于 0
 * @return true 表示已落盘
 * @note   不足 4 字节的部分按字补齐，未覆盖的字节保持 0xFF。
 */
bool app_flash_write(uint32_t address, const uint8_t *data, uint32_t size);

/**
 * @brief  按地址读出数据
 * @param  address 源地址，整段必须落在 Slot A / Slot B 内
 * @param  data    目标缓冲区
 * @param  size    字节数，必须大于 0
 * @return true 表示读取完成
 * @note   用 volatile 逐字节读，避免编译器把回读校验优化成常量。
 */
bool app_flash_read(uint32_t address, uint8_t *data, uint32_t size);

/** @brief 擦除整个参数数据区（sector 4，64 KB） */
bool app_flash_erase_nvs(void);

/**
 * @brief  向参数数据区写入
 * @param  offset 相对 APP_DATA_ADDR 的偏移，必须 4 字节对齐
 * @param  data   数据指针
 * @param  size   字节数，必须大于 0
 * @return true 表示已落盘
 * @note   只保证"写"能落盘，不保证可重复写：同一地址二次写入前必须先擦除
 *         整个 sector（Flash 只能把 1 写成 0）。
 */
bool app_flash_write_nvs(uint32_t offset, const uint8_t *data, uint32_t size);

/**
 * @brief  从参数数据区读出
 * @param  offset 相对 APP_DATA_ADDR 的偏移
 * @param  data   目标缓冲区
 * @param  size   字节数，必须大于 0
 * @return true 表示读取完成
 */
bool app_flash_read_nvs(uint32_t offset, uint8_t *data, uint32_t size);

#endif /* APP_FLASH_H */
