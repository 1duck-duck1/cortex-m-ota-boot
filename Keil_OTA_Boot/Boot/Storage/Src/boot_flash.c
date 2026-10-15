/**
 ******************************************************************************
 * @file    boot_flash.c
 * @brief   Boot 侧 Flash 访问实现
 ******************************************************************************
 */
#include "boot_flash.h"

#include "boot_conf.h"

#include "stm32f4xx_hal.h"

/** 擦写前要清掉的错误标志集合：这份 HAL 没有提供 FLASH_FLAG_ALL_ERRORS，自己拼一个 */
#define BOOT_FLASH_ERROR_FLAGS   (FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | \
                                  FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR)

/**
 * 扇区地址表：STM32F407VGT6 的 12 个扇区，边界必须与 boot_conf.h 的分区表一致。
 * 数组下标即扇区号，boot_flash_sector_of() 靠它把地址翻译成扇区。
 */
static const uint32_t s_sector_address[BOOT_FLASH_SECTOR_TOTAL] =
{
    0x08000000u, 0x08004000u, 0x08008000u, 0x0800C000u,
    0x08010000u, 0x08020000u, 0x08040000u, 0x08060000u,
    0x08080000u, 0x080A0000u, 0x080C0000u, 0x080E0000u
};

/**
 * @brief  把地址翻译成扇区号
 * @param  address Flash 地址
 * @return 扇区号；地址不在片内 Flash 时返回 BOOT_FLASH_SECTOR_INVALID
 */
static uint32_t boot_flash_sector_of(uint32_t address)
{
    uint32_t index;

    if ((address < BOOT_FLASH_BASE) || (address >= (BOOT_FLASH_BASE + BOOT_FLASH_SIZE))) {
        return BOOT_FLASH_SECTOR_INVALID;
    }

    /* 从后往前找最后一个起始地址不大于 address 的扇区：表只有 12 项，
       线性回扫比二分更省代码。 */
    for (index = BOOT_FLASH_SECTOR_TOTAL; index > 0u; index--) {
        if (address >= s_sector_address[index - 1u]) {
            return index - 1u;
        }
    }

    return BOOT_FLASH_SECTOR_INVALID;
}

/**
 * @brief  擦除单个扇区
 * @param  sector 扇区号
 * @return true 表示擦除完成
 * @note   放在 RAM 中执行，见 boot_flash.h 的说明。
 */
BOOT_RAMFUNC static bool boot_flash_erase_sector(uint32_t sector)
{
    FLASH_EraseInitTypeDef init;
    uint32_t error = 0u;
    bool result;

    init.TypeErase = FLASH_TYPEERASE_SECTORS;
    init.Banks = FLASH_BANK_1;
    init.Sector = sector;
    init.NbSectors = 1u;
    init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    (void)HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(BOOT_FLASH_ERROR_FLAGS);

    result = (HAL_FLASHEx_Erase(&init, &error) == HAL_OK);

    (void)HAL_FLASH_Lock();

    return result;
}

/**
 * @brief  按字编程一段数据，调用方负责加解锁
 * @param  address 目标地址，必须 4 字节对齐
 * @param  data    数据指针
 * @param  size    字节数，必须大于 0
 * @return true 表示已落盘
 * @note   尾部不足一个字的字节按 0xFF 补齐，等价于不写，不会破坏该字里已有的内容。
 */
BOOT_RAMFUNC static bool boot_flash_program_words(uint32_t address,
                                                  const uint8_t *data,
                                                  uint32_t size)
{
    uint32_t offset = 0u;
    uint32_t value;
    uint32_t index;
    uint32_t chunk;
    bool result = true;

    while ((offset < size) && result) {
        chunk = size - offset;
        if (chunk > 4u) {
            chunk = 4u;
        }

        value = 0xFFFFFFFFu;
        for (index = 0u; index < chunk; index++) {
            value = (value & ~(0xFFu << (index * 8u))) |
                    ((uint32_t)data[offset + index] << (index * 8u));
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + offset, value) != HAL_OK) {
            result = false;
        }

        offset += 4u;
    }

    return result;
}

uint32_t boot_flash_slot_base(uint32_t slot_id)
{
    if (slot_id == BOOT_SLOT_ID_A) {
        return BOOT_SLOT_A_ADDR;
    }
    if (slot_id == BOOT_SLOT_ID_B) {
        return BOOT_SLOT_B_ADDR;
    }

    return 0u;
}

uint32_t boot_flash_other_slot(uint32_t slot_id)
{
    if (slot_id == BOOT_SLOT_ID_A) {
        return BOOT_SLOT_ID_B;
    }
    if (slot_id == BOOT_SLOT_ID_B) {
        return BOOT_SLOT_ID_A;
    }

    return BOOT_SLOT_ID_NONE;
}

bool boot_flash_range_in_slot(uint32_t address, uint32_t size)
{
    uint32_t end;
    uint32_t base;
    uint32_t index;

    if (size == 0u) {
        return false;
    }

    end = address + size;
    if (end < address) {
        return false;   /* 地址回绕，必然越界 */
    }

    for (index = 0u; index < 2u; index++) {
        base = boot_flash_slot_base(index);
        if ((address >= base) && (end <= (base + BOOT_SLOT_SIZE))) {
            return true;
        }
    }

    return false;
}

bool boot_flash_is_erased(uint32_t address, uint32_t size)
{
    uint32_t index;

    if (size == 0u) {
        return false;
    }

    for (index = 0u; index < size; index++) {
        if (*(volatile uint8_t *)(address + index) != 0xFFu) {
            return false;
        }
    }

    return true;
}

BOOT_RAMFUNC bool boot_flash_erase_slot(uint32_t slot_id)
{
    uint32_t base = boot_flash_slot_base(slot_id);
    uint32_t sector;
    uint32_t index;

    if (base == 0u) {
        return false;
    }

    sector = boot_flash_sector_of(base);
    if (sector == BOOT_FLASH_SECTOR_INVALID) {
        return false;
    }

    for (index = 0u; index < BOOT_SLOT_SECTOR_COUNT; index++) {
        if (!boot_flash_erase_sector(sector + index)) {
            return false;
        }
    }

    return true;
}

BOOT_RAMFUNC bool boot_flash_erase_nvs(void)
{
    uint32_t sector = boot_flash_sector_of(BOOT_NVS_ADDR);

    /* NVS 区必须恰好等于一个扇区：小于会浪费，大于则"擦 NVS"会波及邻居，
       这类几何错误必须在运行期挡住，而不是靠配置时的细心。 */
    if ((sector == BOOT_FLASH_SECTOR_INVALID) ||
        (s_sector_address[sector] != BOOT_NVS_ADDR) ||
        (BOOT_NVS_SIZE != (BOOT_SLOT_A_ADDR - BOOT_NVS_ADDR))) {
        return false;
    }

    return boot_flash_erase_sector(sector);
}

BOOT_RAMFUNC bool boot_flash_write(uint32_t address, const uint8_t *data, uint32_t size)
{
    bool result;

    if ((data == NULL) || (size == 0u) || ((address & 0x3u) != 0u)) {
        return false;
    }
    if (!boot_flash_range_in_slot(address, size)) {
        return false;
    }

    (void)HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(BOOT_FLASH_ERROR_FLAGS);

    result = boot_flash_program_words(address, data, size);

    (void)HAL_FLASH_Lock();

    return result;
}

BOOT_RAMFUNC bool boot_flash_write_metadata(uint32_t copy_index,
                                            const void *data,
                                            uint32_t size)
{
    uint32_t address;
    uint32_t sector;
    bool result;

    if (copy_index == 0u) {
        address = BOOT_METADATA_A_ADDR;
    } else if (copy_index == 1u) {
        address = BOOT_METADATA_B_ADDR;
    } else {
        return false;
    }

    if ((data == NULL) || (size == 0u) || (size > BOOT_METADATA_SIZE)) {
        return false;
    }

    sector = boot_flash_sector_of(address);
    if ((sector == BOOT_FLASH_SECTOR_INVALID) || (s_sector_address[sector] != address)) {
        return false;
    }

    /* 先擦后写：Flash 只能把 1 写成 0，同一份副本二次写入前必须回到全 1。 */
    if (!boot_flash_erase_sector(sector)) {
        return false;
    }

    (void)HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(BOOT_FLASH_ERROR_FLAGS);

    result = boot_flash_program_words(address, (const uint8_t *)data, size);

    (void)HAL_FLASH_Lock();

    return result;
}

BOOT_RAMFUNC bool boot_flash_write_nvs(uint32_t offset, const uint8_t *data, uint32_t size)
{
    bool result;

    if ((data == NULL) || (size == 0u) || ((offset & 0x3u) != 0u)) {
        return false;
    }
    if (offset > (BOOT_NVS_SIZE - size)) {
        return false;
    }

    (void)HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(BOOT_FLASH_ERROR_FLAGS);

    result = boot_flash_program_words(BOOT_NVS_ADDR + offset, data, size);

    (void)HAL_FLASH_Lock();

    return result;
}

bool boot_flash_read(uint32_t address, uint8_t *data, uint32_t size)
{
    uint32_t index;

    if ((data == NULL) || (size == 0u)) {
        return false;
    }

    for (index = 0u; index < size; index++) {
        data[index] = *(volatile uint8_t *)(address + index);
    }

    return true;
}

bool boot_flash_read_nvs(uint32_t offset, uint8_t *data, uint32_t size)
{
    if ((data == NULL) || (size == 0u) || (offset > (BOOT_NVS_SIZE - size))) {
        return false;
    }

    return boot_flash_read(BOOT_NVS_ADDR + offset, data, size);
}
