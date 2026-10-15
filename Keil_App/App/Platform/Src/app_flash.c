/**
 ******************************************************************************
 * @file    app_flash.c
 * @brief   App 侧 Flash 擦除、写入与回读
 *
 * 擦写期间直接运行在 Flash 上，不搬 RAMFUNC：STM32F407 是单 bank Flash，擦写时
 * CPU 取指被总线 stall 而不是取到脏数据或 HardFault，Boot 侧的 Metadata 提交与
 * 槽写入一直在同样条件下工作，因此这里沿用同一写法。
 ******************************************************************************
 */
#include "app_flash.h"

#include "app_config.h"

#include "stm32f4xx_hal.h"

#include <stddef.h>

uint32_t app_flash_slot_base(uint32_t slot_id)
{
    if (slot_id == APP_SLOT_ID_A) {
        return APP_SLOT_A_BASE;
    }
    if (slot_id == APP_SLOT_ID_B) {
        return APP_SLOT_B_BASE;
    }
    return 0u;
}

uint32_t app_flash_slot_size(uint32_t slot_id)
{
    if ((slot_id == APP_SLOT_ID_A) || (slot_id == APP_SLOT_ID_B)) {
        return APP_SLOT_SIZE;
    }
    return 0u;
}

bool app_flash_range_valid(uint32_t address, uint32_t size)
{
    uint32_t base;

    if (size == 0u) {
        return false;
    }
    if (size > (UINT32_MAX - address)) {
        return false;   /* 地址加长度溢出，必须先挡住再比较区间 */
    }

    base = app_flash_slot_base(APP_SLOT_ID_A);
    if ((address >= base) && ((address + size) <= (base + APP_SLOT_SIZE))) {
        return true;
    }
    base = app_flash_slot_base(APP_SLOT_ID_B);
    return (address >= base) && ((address + size) <= (base + APP_SLOT_SIZE));
}

/**
 * @brief  校验 NVS 内的偏移范围
 * @param  offset 相对 APP_DATA_ADDR 的偏移
 * @param  size   字节数
 * @note   先判 size 上限再算 APP_DATA_SIZE - size，避免无符号减法回绕后
 *         把越界偏移误判成合法。
 */
static bool app_flash_nvs_range_valid(uint32_t offset, uint32_t size)
{
    if ((size == 0u) || (size > APP_DATA_SIZE)) {
        return false;
    }
    return offset <= (APP_DATA_SIZE - size);
}

/**
 * @brief  按字编程一段数据
 * @note   调用方已完成地址与范围校验；本函数只保证 4 字节对齐与解锁/上锁配对。
 */
static bool app_flash_program_words(uint32_t address,
                                    const uint8_t *data,
                                    uint32_t size)
{
    uint32_t index = 0u;
    uint32_t i;

    if ((data == NULL) || ((address & 3u) != 0u) ||
        (size > (UINT32_MAX - address))) {
        return false;
    }

    HAL_FLASH_Unlock();
    while (index < size) {
        uint32_t word = 0xFFFFFFFFu;
        uint32_t remaining = size - index;
        uint32_t copy_size = (remaining < 4u) ? remaining : 4u;

        for (i = 0u; i < copy_size; i++) {
            word &= ~(0xFFu << (8u * i));
            word |= ((uint32_t)data[index + i]) << (8u * i);
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + index, word) !=
            HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        index += copy_size;
    }
    HAL_FLASH_Lock();

    return true;
}

/**
 * @brief  擦除一个扇区
 * @param  sector      扇区编号（FLASH_SECTOR_x）
 * @param  sector_count 连续擦除的扇区数
 */
static bool app_flash_erase_sectors(uint32_t sector, uint32_t sector_count)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0u;
    HAL_StatusTypeDef hal_status;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = sector;
    erase.NbSectors = sector_count;

    HAL_FLASH_Unlock();
    hal_status = HAL_FLASHEx_Erase(&erase, &page_error);
    HAL_FLASH_Lock();

    return hal_status == HAL_OK;
}

bool app_flash_erase_slot(uint32_t slot_id)
{
    if (slot_id == APP_SLOT_ID_A) {
        return app_flash_erase_sectors(FLASH_SECTOR_5, APP_SLOT_SECTOR_COUNT);
    }
    if (slot_id == APP_SLOT_ID_B) {
        return app_flash_erase_sectors(FLASH_SECTOR_8, APP_SLOT_SECTOR_COUNT);
    }
    return false;
}

bool app_flash_write(uint32_t address, const uint8_t *data, uint32_t size)
{
    if ((data == NULL) || ((address & 3u) != 0u) ||
        !app_flash_range_valid(address, size)) {
        return false;
    }

    return app_flash_program_words(address, data, size);
}

bool app_flash_read(uint32_t address, uint8_t *data, uint32_t size)
{
    uint32_t index;

    if ((data == NULL) || !app_flash_range_valid(address, size)) {
        return false;
    }

    for (index = 0u; index < size; index++) {
        data[index] = *(volatile uint8_t *)(address + index);
    }

    return true;
}

bool app_flash_erase_nvs(void)
{
    return app_flash_erase_sectors(FLASH_SECTOR_4, 1u);
}

bool app_flash_write_nvs(uint32_t offset, const uint8_t *data, uint32_t size)
{
    if ((data == NULL) || ((offset & 3u) != 0u) ||
        !app_flash_nvs_range_valid(offset, size)) {
        return false;
    }

    return app_flash_program_words(APP_DATA_ADDR + offset, data, size);
}

bool app_flash_read_nvs(uint32_t offset, uint8_t *data, uint32_t size)
{
    uint32_t index;

    if ((data == NULL) || !app_flash_nvs_range_valid(offset, size)) {
        return false;
    }

    for (index = 0u; index < size; index++) {
        data[index] = *(volatile uint8_t *)(APP_DATA_ADDR + offset + index);
    }

    return true;
}
