#include "boot_flash.h"
#include "boot_conf.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>

static bool boot_flash_add_ok(uint32_t address, uint32_t size)
{
    return (size <= (UINT32_MAX - address));
}

static boot_status_t boot_flash_program_words(uint32_t address,
                                              const uint8_t *data,
                                              uint32_t size)
{
    uint32_t index = 0u;

    if ((data == NULL) || ((address & 3u) != 0u) ||
        !boot_flash_add_ok(address, size))
    {
        return BOOT_STATUS_INVALID_PARAM;
    }

    HAL_FLASH_Unlock();
    while (index < size)
    {
        uint32_t word = 0xFFFFFFFFu;
        uint32_t remaining = size - index;
        uint32_t copy_size = (remaining < 4u) ? remaining : 4u;

        for (uint32_t i = 0u; i < copy_size; i++)
        {
            word &= ~(0xFFu << (8u * i));
            word |= ((uint32_t)data[index + i]) << (8u * i);
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              address + index,
                              word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return BOOT_STATUS_FLASH_ERROR;
        }
        index += copy_size;
    }
    HAL_FLASH_Lock();
    return BOOT_STATUS_OK;
}

bool boot_flash_range_valid(uint32_t address, uint32_t size)
{
    if (!boot_flash_add_ok(address, size) || (size == 0u))
    {
        return false;
    }

    if ((address >= BOOT_SLOT_A_ADDR) &&
        ((address + size) <= (BOOT_SLOT_A_ADDR + BOOT_SLOT_A_SIZE)))
    {
        return true;
    }
    return (address >= BOOT_SLOT_B_ADDR) &&
           ((address + size) <= (BOOT_SLOT_B_ADDR + BOOT_SLOT_B_SIZE));
}

static bool boot_flash_metadata_range_valid(uint32_t address, uint32_t size)
{
    return ((address >= BOOT_META_A_ADDR) &&
            ((address + size) <= (BOOT_META_A_ADDR + BOOT_META_COPY_SIZE))) ||
           ((address >= BOOT_META_B_ADDR) &&
            ((address + size) <= (BOOT_META_B_ADDR + BOOT_META_COPY_SIZE)));
}

boot_status_t boot_flash_init(void)
{
    return BOOT_STATUS_OK;
}

boot_status_t boot_flash_erase_slot(boot_slot_id_t slot_id)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0u;
    HAL_StatusTypeDef hal_status;

    if ((slot_id != BOOT_SLOT_ID_A) && (slot_id != BOOT_SLOT_ID_B))
    {
        return BOOT_STATUS_INVALID_PARAM;
    }

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = (slot_id == BOOT_SLOT_ID_A) ? FLASH_SECTOR_5 : FLASH_SECTOR_8;
    erase.NbSectors = 3u;

    HAL_FLASH_Unlock();
    hal_status = HAL_FLASHEx_Erase(&erase, &page_error);
    HAL_FLASH_Lock();

    return (hal_status == HAL_OK) ? BOOT_STATUS_OK : BOOT_STATUS_FLASH_ERROR;
}

boot_status_t boot_flash_write(uint32_t address,
                               const uint8_t *data,
                               uint32_t size)
{
    if ((data == NULL) || !boot_flash_range_valid(address, size) ||
        ((address & 3u) != 0u))
    {
        return BOOT_STATUS_INVALID_PARAM;
    }

    return boot_flash_program_words(address, data, size);
}

boot_status_t boot_flash_erase_metadata(uint32_t address)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0u;
    uint32_t sector;

    if (address == BOOT_META_A_ADDR)
    {
        sector = FLASH_SECTOR_2;
    }
    else if (address == BOOT_META_B_ADDR)
    {
        sector = FLASH_SECTOR_3;
    }
    else
    {
        return BOOT_STATUS_INVALID_PARAM;
    }

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = sector;
    erase.NbSectors = 1u;
    HAL_FLASH_Unlock();
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return BOOT_STATUS_FLASH_ERROR;
    }
    HAL_FLASH_Lock();
    return BOOT_STATUS_OK;
}

boot_status_t boot_flash_write_metadata(uint32_t address,
                                        const uint8_t *data,
                                        uint32_t size)
{
    if ((address != BOOT_META_A_ADDR && address != BOOT_META_B_ADDR) ||
        (size > BOOT_META_COPY_SIZE))
    {
        return BOOT_STATUS_INVALID_PARAM;
    }
    return boot_flash_program_words(address, data, size);
}

boot_status_t boot_flash_read(uint32_t address,
                              uint8_t *data,
                              uint32_t size)
{
    if ((data == NULL) ||
        (!boot_flash_range_valid(address, size) &&
         !boot_flash_metadata_range_valid(address, size)))
    {
        return BOOT_STATUS_INVALID_PARAM;
    }

    for (uint32_t i = 0u; i < size; i++)
    {
        data[i] = *(volatile uint8_t *)(address + i);
    }
    return BOOT_STATUS_OK;
}
