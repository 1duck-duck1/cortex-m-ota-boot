/**
 ******************************************************************************
 * @file    boot_metadata.h
 * @brief   Metadata 追加日志：active / pending 槽记录与试运行计数
 *
 * Metadata 是"哪个槽该跑"的唯一权威，落在 sector 11 的追加日志里：
 * 每条记录占 32 字节，按顺序往后写，写满整个扇区才整扇区擦除回收一次。
 * 追加写本身是掉电安全的——半截记录 CRC32 不通过，开机扫描取
 * "crc32 自洽且 sequence 最大"的一条即可，前一条仍然有效。
 * 唯一的无记录窗口是日志写满后整扇区擦除到新记录写完之间，此时设备会以
 * 默认值（无 active / pending）停在升级模式，可恢复但不会自动启动 App。
 *
 * 记录里只放槽编号与计数，不放版本号与长度：那些属于镜像头，从镜像本身读，
 * 避免同一份信息在两个地方各存一份后慢慢分歧。
 ******************************************************************************
 */
#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_conf.h"

/** Metadata 的落盘布局，整体 32 字节，作为追加日志的一条记录写入 */
typedef struct
{
    uint32_t magic;         /**< BOOT_METADATA_MAGIC */
    uint32_t sequence;      /**< 单调递增序号，取最大的一份为最新 */
    uint32_t active_slot;   /**< 当前稳定运行的槽；无有效镜像时为 BOOT_SLOT_ID_NONE */
    uint32_t pending_slot;  /**< 待确认的槽；无待确认镜像时为 BOOT_SLOT_ID_NONE */
    uint32_t boot_attempts; /**< pending 槽已试运行次数 */
    uint32_t max_attempts;  /**< 试运行次数上限，超过即回滚 */
    uint32_t reserved;      /**< 预留，必须写 0 */
    uint32_t crc32;         /**< 记录 CRC32，覆盖本字段之前的 28 字节 */
} boot_metadata_t;

/**
 * @brief  读出最新的 Metadata
 * @param  metadata 读出结果，不可为 NULL
 * @return true 表示日志里确实存在一条自洽记录
 * @retval false 日志里没有可用记录，此时 metadata 已被填入默认值
 * @note   无论返回值如何，metadata 都会被填成一份可直接使用的内容：
 *         找不到记录时用 boot_metadata_defaults() 兜底（active / pending 均为
 *         NONE，试运行上限取 BOOT_DEFAULT_MAX_ATTEMPTS）。返回值只表达
 *         "盘上有没有"，供启动决策区分"首次上电"与"读到过记录"。
 */
bool boot_metadata_load(boot_metadata_t *metadata);

/**
 * @brief  把一份 Metadata 落盘
 * @param  metadata 待落盘的内容，sequence 字段被忽略
 * @return true 表示已落盘且可被读出
 * @retval false 写入或回读校验失败
 * @note   记录追加到日志的下一个擦除态位置；只有日志写满时才整扇区擦除回收。
 *         新序号取日志中最大序号加一。crc32 由本函数统一计算与填写，
 *         调用方填的值无效。
 */
bool boot_metadata_save(const boot_metadata_t *metadata);

/**
 * @brief  用默认值填充一份 Metadata
 * @param  metadata 待填充的内容，不可为 NULL
 * @note   默认值 = 无 active 槽、无 pending 槽、试运行上限取
 *         BOOT_DEFAULT_MAX_ATTEMPTS，等价于"这台设备还没装过固件"。
 */
void boot_metadata_defaults(boot_metadata_t *metadata);

/**
 * @brief  把某个槽标记为待确认
 * @param  metadata 目标记录，不可为 NULL
 * @param  slot_id  待确认的槽
 * @return true 表示内容有变化
 * @retval false slot_id 非法或该槽已是 pending
 * @note   只改内存，不落盘；由调用方在合适时机调用 boot_metadata_save()。
 *         试运行计数在这里清零，让新镜像有完整的重试次数。
 */
bool boot_metadata_mark_pending(boot_metadata_t *metadata, uint32_t slot_id);

/**
 * @brief  确认 pending 槽试运行成功
 * @param  metadata 目标记录，不可为 NULL
 * @return true 表示内容有变化
 * @note   把 pending 转正为 active 并清空试运行计数；没有 pending 时返回 false。
 *         只改内存，不落盘。
 */
bool boot_metadata_confirm(boot_metadata_t *metadata);

/**
 * @brief  放弃 pending 槽，回到回滚状态
 * @param  metadata 目标记录，不可为 NULL
 * @return true 表示内容有变化
 * @note   只清 pending 与计数，active 保持不动——回滚就是什么都不做，
 *         下次启动自然会选回 active。只改内存，不落盘。
 */
bool boot_metadata_clear_pending(boot_metadata_t *metadata);

/**
 * @brief  设置 pending 槽的试运行次数
 * @param  metadata 目标记录，不可为 NULL
 * @param  attempts 已试运行次数
 * @return true 表示内容有变化
 * @note   只改内存，不落盘。
 */
bool boot_metadata_set_attempts(boot_metadata_t *metadata, uint32_t attempts);

#endif /* BOOT_METADATA_H */
