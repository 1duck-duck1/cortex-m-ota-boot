#include "boot_flash.h"
#include "boot_conf.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>

/*
 * Flash 擦写为寄存器级实现（架构文档 §8.4），两条硬性理由：
 * 1. 超时不依赖 SysTick：HAL 的 FLASH_WaitForLastOperation 以
 *    HAL_GetTick() 计超时，而擦写期间取指 stall、tick 冻结，
 *    Flash 出错时超时永不触发，退化为无界死循环。此处改为
 *    有界循环计数轮询 BSY，错误时保证退出。
 * 2. 调用链可控：RAMFUNC 约束（§8.1）将来引入时只涉及本文件。
 *
 * 注意：擦写函数本身仍在 Flash 中执行——F4 擦写期间取指会
 * stall 直到操作完成（RM0090 §3.5.5），Bootloader 单任务环境
 * 下该延迟可接受；待 v0.3 引入中断驱动的传输层时再迁 RAMFUNC。
 */

#define BOOT_FLASH_KEY1         0x45670123u
#define BOOT_FLASH_KEY2         0xCDEF89ABu
/* 有界轮询上限：约 5 秒 @168MHz，覆盖 128 KB sector 最坏擦除时间。 */
#define BOOT_FLASH_TIMEOUT_LOOP 100000000u

/* 擦除/编程错误标志（写 1 清零）。 */
#define BOOT_FLASH_ERR_MASK     (FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                                 FLASH_SR_PGPERR | FLASH_SR_PGSERR)

static bool boot_flash_add_ok(uint32_t address, uint32_t size)
{
    return (size <= (UINT32_MAX - address));
}

static void boot_flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u)
    {
        FLASH->KEYR = BOOT_FLASH_KEY1;
        FLASH->KEYR = BOOT_FLASH_KEY2;
    }
}

static void boot_flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

/** @brief 有界等待 BSY 清零；false 表示超时（Flash 出错时保证退出）。 */
static bool boot_flash_wait_idle(void)
{
    uint32_t guard = BOOT_FLASH_TIMEOUT_LOOP;

    while ((FLASH->SR & FLASH_SR_BSY) != 0u)
    {
        if (--guard == 0u)
        {
            return false;
        }
    }
    return true;
}

static void boot_flash_clear_flags(void)
{
    FLASH->SR = FLASH_SR_EOP | BOOT_FLASH_ERR_MASK;
}

static bool boot_flash_has_error(void)
{
    return (FLASH->SR & BOOT_FLASH_ERR_MASK) != 0u;
}

/** @brief 擦除单个 sector（调用方保证已解锁）。 */
static bool boot_flash_erase_sector_unlocked(uint32_t sector)
{
    bool ok;

    if (!boot_flash_wait_idle())
    {
        return false;
    }
    boot_flash_clear_flags();

    FLASH->CR = (FLASH->CR & ~(FLASH_CR_SNB | FLASH_CR_PG)) |
                FLASH_CR_SER |
                ((sector << FLASH_CR_SNB_Pos) & FLASH_CR_SNB);
    FLASH->CR |= FLASH_CR_STRT;

    ok = boot_flash_wait_idle();
    FLASH->CR &= ~FLASH_CR_SER;
    if (!ok || boot_flash_has_error())
    {
        boot_flash_clear_flags();
        return false;
    }
    return true;
}

/** @brief 按 32 位字编程一次（调用方保证已解锁）。 */
static bool boot_flash_program_word_unlocked(uint32_t address, uint32_t word)
{
    bool ok;

    if (!boot_flash_wait_idle())
    {
        return false;
    }
    boot_flash_clear_flags();

    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_PSIZE_WORD | FLASH_CR_PG;
    __DSB();
    *(volatile uint32_t *)address = word;

    ok = boot_flash_wait_idle();
    FLASH->CR &= ~FLASH_CR_PG;
    if (!ok || boot_flash_has_error())
    {
        boot_flash_clear_flags();
        return false;
    }
    return true;
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

    boot_flash_unlock();
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

        if (!boot_flash_program_word_unlocked(address + index, word))
        {
            boot_flash_lock();
            return BOOT_STATUS_FLASH_ERROR;
        }
        index += copy_size;
    }
    boot_flash_lock();
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
    uint32_t first_sector;
    uint32_t i;

    if ((slot_id != BOOT_SLOT_ID_A) && (slot_id != BOOT_SLOT_ID_B))
    {
        return BOOT_STATUS_INVALID_PARAM;
    }

    /* Slot A 占 sector 5-7，Slot B 占 sector 8-10。 */
    first_sector = (slot_id == BOOT_SLOT_ID_A) ? FLASH_SECTOR_5 : FLASH_SECTOR_8;

    boot_flash_unlock();
    for (i = 0u; i < 3u; i++)
    {
        if (!boot_flash_erase_sector_unlocked(first_sector + i))
        {
            boot_flash_lock();
            return BOOT_STATUS_FLASH_ERROR;
        }
    }
    boot_flash_lock();
    return BOOT_STATUS_OK;
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

    boot_flash_unlock();
    if (!boot_flash_erase_sector_unlocked(sector))
    {
        boot_flash_lock();
        return BOOT_STATUS_FLASH_ERROR;
    }
    boot_flash_lock();
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
