/**
 ******************************************************************************
 * @file    boot_metadata.c
 * @brief   Metadata 双副本读写实现
 ******************************************************************************
 */
#include "boot_metadata.h"

#include <string.h>

#include "boot_flash.h"
#include "ota_crc32.h"

/** 参与 crc32 计算的字节数：整条记录去掉末尾的 crc32 字段 */
#define BOOT_METADATA_CRC_SPAN     ((uint32_t)(sizeof(boot_metadata_t) - 4u))

/**
 * @brief  计算一条 Metadata 的 crc32
 * @param  metadata 待计算的记录
 * @return CRC32，与所属副本的 crc32 字段比较
 */
static uint32_t boot_metadata_crc(const boot_metadata_t *metadata)
{
    return ota_crc32_calc((const uint8_t *)metadata, BOOT_METADATA_CRC_SPAN);
}

/**
 * @brief  校验一条 Metadata 的自洽性
 * @param  metadata 待校验的记录
 * @return true 表示魔术、槽编号、计数与 crc32 全部通过
 */
static bool boot_metadata_verify(const boot_metadata_t *metadata)
{
    if (metadata->magic != BOOT_METADATA_MAGIC)
    {
        return false;
    }

    /* active 只允许是具体槽或 NONE；pending 同理，且不允许等于 active。 */
    if ((metadata->active_slot != BOOT_SLOT_ID_A) &&
        (metadata->active_slot != BOOT_SLOT_ID_B) &&
        (metadata->active_slot != BOOT_SLOT_ID_NONE))
    {
        return false;
    }
    if ((metadata->pending_slot != BOOT_SLOT_ID_A) &&
        (metadata->pending_slot != BOOT_SLOT_ID_B) &&
        (metadata->pending_slot != BOOT_SLOT_ID_NONE))
    {
        return false;
    }
    if (metadata->pending_slot == metadata->active_slot)
    {
        return false;
    }

    if ((metadata->max_attempts == 0u) ||
        (metadata->boot_attempts > metadata->max_attempts))
    {
        return false;
    }

    return (metadata->crc32 == boot_metadata_crc(metadata));
}

/**
 * @brief  读出指定副本并校验
 * @param  copy_index 副本编号，0 为 Metadata A，1 为 Metadata B
 * @param  metadata   读出结果，不可为 NULL
 * @return true 表示该副本存在且自洽
 * @note   读失败或不自洽时，metadata 会被清零，避免调用方误用半截内容。
 */
static bool boot_metadata_read_copy(uint32_t copy_index, boot_metadata_t *metadata)
{
    uint32_t address;

    (void)memset(metadata, 0, sizeof(*metadata));

    if (copy_index == 0u)
    {
        address = BOOT_METADATA_A_ADDR;
    }
    else if (copy_index == 1u)
    {
        address = BOOT_METADATA_B_ADDR;
    }
    else
    {
        return false;
    }

    if (!boot_flash_read(address, (uint8_t *)metadata, (uint32_t)sizeof(*metadata)))
    {
        return false;
    }

    if (!boot_metadata_verify(metadata))
    {
        (void)memset(metadata, 0, sizeof(*metadata));
        return false;
    }

    return true;
}

bool boot_metadata_load(boot_metadata_t *metadata)
{
    boot_metadata_t copy_a;
    boot_metadata_t copy_b;
    bool valid_a;
    bool valid_b;

    if (metadata == NULL)
    {
        return false;
    }

    valid_a = boot_metadata_read_copy(0u, &copy_a);
    valid_b = boot_metadata_read_copy(1u, &copy_b);

    if (valid_a && valid_b)
    {
        /* 两份都自洽时取序号大的；序号相同说明写入侧出了问题，取 A 保守处理。 */
        *metadata = (copy_a.sequence >= copy_b.sequence) ? copy_a : copy_b;
        return true;
    }
    if (valid_a)
    {
        *metadata = copy_a;
        return true;
    }
    if (valid_b)
    {
        *metadata = copy_b;
        return true;
    }

    boot_metadata_defaults(metadata);

    return false;
}

bool boot_metadata_save(const boot_metadata_t *metadata)
{
    boot_metadata_t copy_a;
    boot_metadata_t copy_b;
    boot_metadata_t record;
    uint32_t valid_a;
    uint32_t valid_b;
    uint32_t sequence_a;
    uint32_t sequence_b;
    uint32_t max_sequence;
    uint32_t target;
    bool written;

    if (metadata == NULL)
    {
        return false;
    }

    valid_a = boot_metadata_read_copy(0u, &copy_a) ? 1u : 0u;
    valid_b = boot_metadata_read_copy(1u, &copy_b) ? 1u : 0u;

    /* 作废副本的序号按 0 处理：擦除态读出来是 0xFFFFFFFF，直接取最大值会溢出。 */
    sequence_a = (valid_a != 0u) ? copy_a.sequence : 0u;
    sequence_b = (valid_b != 0u) ? copy_b.sequence : 0u;

    /* 只写序号较小的那份，把最新的一份完整留在盘上。 */
    if (valid_a == 0u)
    {
        target = 0u;
    }
    else if (valid_b == 0u)
    {
        target = 1u;
    }
    else
    {
        target = (sequence_a <= sequence_b) ? 0u : 1u;
    }

    max_sequence = (sequence_a > sequence_b) ? sequence_a : sequence_b;
    if (max_sequence == 0xFFFFFFFFu)
    {
        max_sequence = 0u;
    }

    record = *metadata;
    record.magic = BOOT_METADATA_MAGIC;
    record.sequence = max_sequence + 1u;
    if (record.reserved != 0u)
    {
        record.reserved = 0u;
    }
    record.crc32 = boot_metadata_crc(&record);

    if (!boot_flash_write_metadata(target, &record, (uint32_t)sizeof(record)))
    {
        return false;
    }

    /* 回读校验：Flash 编程失败未必有返回值，读回来对不上就不能认。 */
    written = boot_metadata_read_copy(target, &copy_a);

    return written && (copy_a.sequence == record.sequence);
}

void boot_metadata_defaults(boot_metadata_t *metadata)
{
    if (metadata == NULL)
    {
        return;
    }

    (void)memset(metadata, 0, sizeof(*metadata));
    metadata->magic = BOOT_METADATA_MAGIC;
    metadata->sequence = 1u;
    metadata->active_slot = BOOT_SLOT_ID_NONE;
    metadata->pending_slot = BOOT_SLOT_ID_NONE;
    metadata->boot_attempts = 0u;
    metadata->max_attempts = BOOT_DEFAULT_MAX_ATTEMPTS;
    metadata->reserved = 0u;
    metadata->crc32 = boot_metadata_crc(metadata);
}

bool boot_metadata_mark_pending(boot_metadata_t *metadata, uint32_t slot_id)
{
    if (metadata == NULL)
    {
        return false;
    }
    if ((slot_id != BOOT_SLOT_ID_A) && (slot_id != BOOT_SLOT_ID_B))
    {
        return false;
    }
    if (metadata->pending_slot == slot_id)
    {
        return false;
    }

    metadata->pending_slot = slot_id;
    metadata->boot_attempts = 0u;

    return true;
}

bool boot_metadata_confirm(boot_metadata_t *metadata)
{
    if (metadata == NULL)
    {
        return false;
    }
    if ((metadata->pending_slot != BOOT_SLOT_ID_A) &&
        (metadata->pending_slot != BOOT_SLOT_ID_B))
    {
        return false;
    }

    metadata->active_slot = metadata->pending_slot;
    metadata->pending_slot = BOOT_SLOT_ID_NONE;
    metadata->boot_attempts = 0u;

    return true;
}

bool boot_metadata_clear_pending(boot_metadata_t *metadata)
{
    if (metadata == NULL)
    {
        return false;
    }
    if ((metadata->pending_slot == BOOT_SLOT_ID_NONE) &&
        (metadata->boot_attempts == 0u))
    {
        return false;
    }

    metadata->pending_slot = BOOT_SLOT_ID_NONE;
    metadata->boot_attempts = 0u;

    return true;
}

bool boot_metadata_set_attempts(boot_metadata_t *metadata, uint32_t attempts)
{
    if (metadata == NULL)
    {
        return false;
    }
    if (metadata->boot_attempts == attempts)
    {
        return false;
    }

    metadata->boot_attempts = attempts;

    return true;
}
