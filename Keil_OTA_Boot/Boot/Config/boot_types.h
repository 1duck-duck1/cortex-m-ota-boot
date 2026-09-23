/**
 ******************************************************************************
 * @file    boot_types.h
 * @brief   Boot 各模块共用的枚举与状态快照
 *
 * 只放"跨模块都要用"的类型：启动决策的结果、以及供 info / slot 命令输出的
 * 状态快照。具体模块的内部状态一律留在各自的头文件里。
 ******************************************************************************
 */
#ifndef BOOT_TYPES_H
#define BOOT_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/** 本次启动的决策结果 */
typedef enum
{
    BOOT_DECISION_WAIT_UPDATE = 0,  /**< 无可用镜像，停在升级模式等命令行 */
    BOOT_DECISION_RUN_ACTIVE,       /**< 正常启动 active 槽 */
    BOOT_DECISION_RUN_PENDING,      /**< 试运行 pending 槽，等 App 回确认 */
    BOOT_DECISION_RUN_RECOVER       /**< active 槽镜像失效，自愈到另一槽 */
} boot_decision_t;

/** 启动决策的快照，供命令行输出与调试观察 */
typedef struct
{
    boot_decision_t decision;   /**< 本次启动的决策结果 */
    uint32_t active_slot;       /**< Metadata 记录的 active 槽 */
    uint32_t pending_slot;      /**< Metadata 记录的 pending 槽 */
    uint32_t running_slot;      /**< 本次实际选中的槽，未选定时为 BOOT_SLOT_ID_NONE */
    uint32_t boot_attempts;     /**< pending 镜像已试运行次数 */
    uint32_t max_attempts;      /**< 试运行次数上限 */
    bool is_metadata_valid;     /**< 是否读到一份自洽的 Metadata */
    bool is_pending_launch;     /**< 本次是否为 pending 试运行启动 */
} boot_status_t;

#endif /* BOOT_TYPES_H */
