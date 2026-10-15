/**
 ******************************************************************************
 * @file    boot_mailbox.c
 * @brief   跨复位邮箱的实现
 ******************************************************************************
 */
#include "boot_mailbox.h"

#include "boot_conf.h"
#include "ota_crc32.h"
#include "stm32f4xx_hal.h"

/** CRC32 覆盖的字节数：整块去掉末尾的 crc32 字段 */
#define BOOT_MAILBOX_CRC_SPAN   ((uint32_t)(sizeof(boot_mailbox_t) - 4u))

/** 邮箱布局必须恰好占满预留的 32 字节，多一个字节就会踩到 SRAM2 的栈区 */
typedef char boot_mailbox_size_check[(sizeof(boot_mailbox_t) == BOOT_MAILBOX_SIZE) ? 1 : -1];

static volatile boot_mailbox_t *const s_mailbox = (volatile boot_mailbox_t *)BOOT_MAILBOX_ADDR;

/**
 * @brief  计算邮箱内容的 CRC32
 * @return CRC32，与 crc32 字段比较
 * @note   逐字节算而不是逐字算：ota_crc32 与 App 侧的逐字实现在小端下等价，
 *         共用同一份实现可以避免两边自己写一遍后慢慢分歧。
 */
static uint32_t boot_mailbox_crc(void)
{
    return ota_crc32_calc((const uint8_t *)&s_mailbox->magic, BOOT_MAILBOX_CRC_SPAN);
}

/**
 * @brief  把 magic 与 crc32 写回去，使整块内容生效
 * @note   先写字段再写 crc32，最后加一道数据同步屏障，保证复位前内容已落到 SRAM。
 */
static void boot_mailbox_commit(void)
{
    s_mailbox->magic = BOOT_MAILBOX_MAGIC;
    s_mailbox->crc32 = boot_mailbox_crc();
    __DSB();
}

bool boot_mailbox_is_valid(void)
{
    return (s_mailbox->magic == BOOT_MAILBOX_MAGIC) && (s_mailbox->crc32 == boot_mailbox_crc());
}

bool boot_mailbox_has_request(void)
{
    return boot_mailbox_is_valid() && (s_mailbox->request == BOOT_MAILBOX_REQUEST);
}

bool boot_mailbox_has_confirm(void)
{
    return boot_mailbox_is_valid() && (s_mailbox->confirmation == BOOT_MAILBOX_CONFIRM);
}

void boot_mailbox_consume_request(void)
{
    if (s_mailbox->request == 0u) {
        return;
    }

    s_mailbox->request = 0u;
    boot_mailbox_commit();
}

void boot_mailbox_consume_confirm(void)
{
    if (s_mailbox->confirmation == 0u) {
        return;
    }

    s_mailbox->confirmation = 0u;
    boot_mailbox_commit();
}

void boot_mailbox_reset(void)
{
    if (boot_mailbox_is_valid()) {
        return;
    }

    s_mailbox->request = 0u;
    s_mailbox->confirmation = 0u;
    s_mailbox->attempts = 0u;
    s_mailbox->heartbeat = 0u;
    s_mailbox->launched_slot = BOOT_SLOT_ID_NONE;
    s_mailbox->launch_flags = 0u;

    boot_mailbox_commit();
}

void boot_mailbox_publish(uint32_t launched_slot, uint32_t launch_flags, uint32_t attempts)
{
    s_mailbox->attempts = attempts;
    s_mailbox->launched_slot = launched_slot;
    s_mailbox->launch_flags = launch_flags;

    boot_mailbox_commit();
}

uint32_t boot_mailbox_heartbeat(void)
{
    if (!boot_mailbox_is_valid()) {
        return 0u;
    }

    return s_mailbox->heartbeat;
}

uint32_t boot_mailbox_attempts(void)
{
    if (!boot_mailbox_is_valid()) {
        return 0u;
    }

    return s_mailbox->attempts;
}

uint32_t boot_mailbox_launched_slot(void)
{
    if (!boot_mailbox_is_valid()) {
        return BOOT_SLOT_ID_NONE;
    }

    return s_mailbox->launched_slot;
}

uint32_t boot_mailbox_launch_flags(void)
{
    if (!boot_mailbox_is_valid()) {
        return 0u;
    }

    return s_mailbox->launch_flags;
}
