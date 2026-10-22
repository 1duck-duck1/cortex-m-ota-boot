/**
 ******************************************************************************
 * @file    boot_metadata.c
 * @brief   Metadata 追加日志读写实现
 ******************************************************************************
 */
#include "boot_metadata.h"

#include "boot_flash.h"
#include "ota_crc32.h"

#include <string.h>

/** 参与 crc32 计算的字节数：整条记录去掉末尾的 crc32 字段 */
#define BOOT_METADATA_CRC_SPAN     ((uint32_t)(sizeof(boot_metadata_t) - 4u))

/** 单条记录在日志中的占位与整个扇区能容纳的条数 */
#define BOOT_METADATA_SLOT_SIZE    ((uint32_t)sizeof(boot_metadata_t))
#define BOOT_METADATA_SLOT_COUNT   (BOOT_METADATA_SIZE / BOOT_METADATA_SLOT_SIZE)

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
    if (metadata->magic != BOOT_METADATA_MAGIC) {
        return false;
    }

    /* active 只允许是具体槽或 NONE；pending 同理，且不允许等于 active。 */
    if ((metadata->active_slot != BOOT_SLOT_ID_A) &&
        (metadata->active_slot != BOOT_SLOT_ID_B) &&
        (metadata->active_slot != BOOT_SLOT_ID_NONE)) {
        return false;
    }
    if ((metadata->pending_slot != BOOT_SLOT_ID_A) &&
        (metadata->pending_slot != BOOT_SLOT_ID_B) &&
        (metadata->pending_slot != BOOT_SLOT_ID_NONE)) {
        return false;
    }
    if (metadata->pending_slot == metadata->active_slot) {
        return false;
    }

    if ((metadata->max_attempts == 0u) ||
        (metadata->boot_attempts > metadata->max_attempts)) {
        return false;
    }

    return (metadata->crc32 == boot_metadata_crc(metadata));
}

/** 日志扫描结果缓存：load 之后 save 可直接续写，避免每次重复扫描整个扇区 */
static uint32_t s_next_slot;
static uint32_t s_max_sequence;
static bool s_log_ready;

/**
 * @brief  取日志中第 index 条记录的地址
 * @param  index 记录下标
 * @return 记录地址
 */
static uint32_t boot_metadata_slot_address(uint32_t index)
{
    return BOOT_METADATA_ADDR + (index * BOOT_METADATA_SLOT_SIZE);
}

/**
 * @brief  扫描整个日志，找出最新记录与下一条可写位置
 * @param  latest       最新记录，找到时写入
 * @param  max_sequence 最新记录的序号
 * @param  next_slot    第一个处于擦除态的记录位；日志写满时为总数
 * @return true 表示至少有一条自洽记录
 * @note   不依赖"第一条擦除态记录就是日志末尾"的假设：编程失败的记录位会
 *         变成非擦除态的残迹，所以必须扫完整个扇区。
 */
static bool boot_metadata_scan(boot_metadata_t *latest,
                               uint32_t *max_sequence,
                               uint32_t *next_slot)
{
    boot_metadata_t record;
    uint32_t magic;
    uint32_t address;
    uint32_t index;
    bool found = false;

    *max_sequence = 0u;
    *next_slot = BOOT_METADATA_SLOT_COUNT;

    for (index = 0u; index < BOOT_METADATA_SLOT_COUNT; index++) {
        address = boot_metadata_slot_address(index);

        if (!boot_flash_read(address, (uint8_t *)&magic, (uint32_t)sizeof(magic))) {
            return found;
        }
        if (magic == 0xFFFFFFFFu) {
            if (*next_slot == BOOT_METADATA_SLOT_COUNT) {
                *next_slot = index;
            }
            continue;
        }

        if (!boot_flash_read(address, (uint8_t *)&record, (uint32_t)sizeof(record))) {
            return found;
        }
        if (!boot_metadata_verify(&record)) {
            continue;
        }

        if (!found || (record.sequence >= *max_sequence)) {
            *latest = record;
            *max_sequence = record.sequence;
            found = true;
        }
    }

    return found;
}

/**
 * @brief  把写指针推进到下一个擦除态记录位
 * @note   编程失败会在当前位置留下非擦除态残迹，不能直接复用。
 */
static void boot_metadata_seek_free(void)
{
    uint32_t magic;

    while (s_next_slot < BOOT_METADATA_SLOT_COUNT) {
        if (!boot_flash_read(boot_metadata_slot_address(s_next_slot),
                             (uint8_t *)&magic, (uint32_t)sizeof(magic))) {
            s_next_slot = BOOT_METADATA_SLOT_COUNT;
            return;
        }
        if (magic == 0xFFFFFFFFu) {
            return;
        }
        s_next_slot++;
    }
}

bool boot_metadata_load(boot_metadata_t *metadata)
{
    boot_metadata_t latest;
    uint32_t max_sequence;
    uint32_t next_slot;
    bool found;

    if (metadata == NULL) {
        return false;
    }

    (void)memset(&latest, 0, sizeof(latest));
    found = boot_metadata_scan(&latest, &max_sequence, &next_slot);

    s_next_slot = next_slot;
    s_max_sequence = max_sequence;
    s_log_ready = true;

    if (found) {
        *metadata = latest;
        return true;
    }

    boot_metadata_defaults(metadata);

    return false;
}

bool boot_metadata_save(const boot_metadata_t *metadata)
{
    boot_metadata_t record;
    boot_metadata_t readback;
    uint32_t address;
    uint32_t sequence;

    if (metadata == NULL) {
        return false;
    }

    if (!s_log_ready) {
        boot_metadata_t latest;
        uint32_t max_sequence;
        uint32_t next_slot;

        (void)memset(&latest, 0, sizeof(latest));
        (void)boot_metadata_scan(&latest, &max_sequence, &next_slot);
        s_next_slot = next_slot;
        s_max_sequence = max_sequence;
        s_log_ready = true;
    }

    boot_metadata_seek_free();

    /* 日志写满才回收整个扇区；这一步之后到新记录写完之前是唯一的无记录窗口。 */
    if (s_next_slot >= BOOT_METADATA_SLOT_COUNT) {
        if (!boot_flash_erase_metadata()) {
            return false;
        }
        s_next_slot = 0u;
    }

    sequence = s_max_sequence;
    if (sequence == 0xFFFFFFFFu) {
        sequence = 0u;
    }

    record = *metadata;
    record.magic = BOOT_METADATA_MAGIC;
    record.sequence = sequence + 1u;
    if (record.reserved != 0u) {
        record.reserved = 0u;
    }
    record.crc32 = boot_metadata_crc(&record);

    address = boot_metadata_slot_address(s_next_slot);
    if (!boot_flash_write_metadata(address, &record, (uint32_t)sizeof(record))) {
        s_next_slot++;
        return false;
    }

    /* 回读校验：Flash 编程失败未必有返回值，读回来对不上就不能认。 */
    if (!boot_flash_read(address, (uint8_t *)&readback, (uint32_t)sizeof(readback))) {
        s_next_slot++;
        return false;
    }
    if (!boot_metadata_verify(&readback) || (readback.sequence != record.sequence)) {
        s_next_slot++;
        return false;
    }

    s_next_slot++;
    s_max_sequence = record.sequence;

    return true;
}

void boot_metadata_defaults(boot_metadata_t *metadata)
{
    if (metadata == NULL) {
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
    if (metadata == NULL) {
        return false;
    }
    if ((slot_id != BOOT_SLOT_ID_A) && (slot_id != BOOT_SLOT_ID_B)) {
        return false;
    }
    if (metadata->pending_slot == slot_id) {
        return false;
    }

    metadata->pending_slot = slot_id;
    metadata->boot_attempts = 0u;

    return true;
}

bool boot_metadata_confirm(boot_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }
    if ((metadata->pending_slot != BOOT_SLOT_ID_A) &&
        (metadata->pending_slot != BOOT_SLOT_ID_B)) {
        return false;
    }

    metadata->active_slot = metadata->pending_slot;
    metadata->pending_slot = BOOT_SLOT_ID_NONE;
    metadata->boot_attempts = 0u;

    return true;
}

bool boot_metadata_clear_pending(boot_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }
    if ((metadata->pending_slot == BOOT_SLOT_ID_NONE) &&
        (metadata->boot_attempts == 0u)) {
        return false;
    }

    metadata->pending_slot = BOOT_SLOT_ID_NONE;
    metadata->boot_attempts = 0u;

    return true;
}

bool boot_metadata_set_attempts(boot_metadata_t *metadata, uint32_t attempts)
{
    if (metadata == NULL) {
        return false;
    }
    if (metadata->boot_attempts == attempts) {
        return false;
    }

    metadata->boot_attempts = attempts;

    return true;
}
