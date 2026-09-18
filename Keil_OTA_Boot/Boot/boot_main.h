/**
 ******************************************************************************
 * @file    boot_main.h
 * @brief   Bootloader 主流程
 ******************************************************************************
 */
#ifndef BOOT_MAIN_H
#define BOOT_MAIN_H

#include "boot_types.h"

/** @brief Bootloader 当前运行状态（RAM 状态，不直接持久化） */
typedef enum
{
    BOOT_STATE_INIT = 0,                  /**< 初始化状态 */
    BOOT_STATE_LOAD_METADATA,             /**< 加载元数据状态 */
    BOOT_STATE_LOAD_FLAGS,                /**< 加载标志状态 */
    BOOT_STATE_DECIDE,                    /**< 决策状态 */
    BOOT_STATE_PREPARE_BOOT,              /**< 准备引导状态 */
    BOOT_STATE_BOOT_APP,                  /**< 引导应用状态 */
    BOOT_STATE_WAIT_UPDATE,               /**< 等待升级状态 */
    BOOT_STATE_ERROR,                     /**< 错误状态 */
} boot_state_t;

/** @brief Bootloader 停留原因，供调试和后续升级协议使用 */
typedef enum
{
    BOOT_WAIT_REASON_NONE = 0,              /**< 无等待原因 */
    BOOT_WAIT_REASON_UPDATE_REQUESTED,      /**< 升级文件请求升级 */
    BOOT_WAIT_REASON_ATTEMPTS_EXCEEDED,     /**< 引导尝试次数超过最大限制 */
    BOOT_WAIT_REASON_INVALID_IMAGE,         /**< 无效的升级文件 */
    BOOT_WAIT_REASON_INTERNAL_ERROR,        /**< 内部错误 */
} boot_wait_reason_t;

/**
 * @brief Bootloader 运行时上下文
 * @note  该结构只存在于 RAM，不得直接序列化到 Flash 元数据区。
 */
typedef struct
{
    boot_state_t state;                 /**< 当前运行状态 */
    boot_state_t previous_state;        /**< 上一个运行状态 */
    boot_status_t last_status;          /**< 上一个状态的运行结果 */
    boot_wait_reason_t wait_reason;     /**< 当前等待原因 */
    boot_slot_t boot_slot;              /**< 当前引导槽 */
    boot_slot_t target_slot;            /**< 目标槽 */
    boot_metadata_t metadata;           /**< 应用元数据 */
    uint32_t boot_attempts;             /**< 引导尝试次数 */
    uint32_t update_version;            /**< 升级版本号 */
    uint32_t update_size;               /**< 升级文件大小 */
    uint32_t update_crc32;              /**< 升级文件 CRC32 校验值 */
    uint32_t received_size;             /**< 已接收数据大小 */
    uint32_t running_crc32;             /**< 当前运行 CRC32 校验值 */
    bool update_requested;              /**< 是否请求升级 */
    bool app_confirmed;                 /**< 是否确认应用 */
} boot_context_t;

/** @brief 初始化 Bootloader 状态机上下文 */
boot_status_t boot_context_init(boot_context_t *context);

/** @brief 执行一次状态机处理 */
boot_status_t boot_process(boot_context_t *context);

/** @brief 获取 boot_run() 使用的只读运行时上下文 */
const boot_context_t *boot_get_context(void);

/**
 * @brief  Bootloader 主流程，正常情况下不返回
 * @note   在 main.c 的 USER CODE 2 区调用（HAL_Init 与时钟配置之后）
 */
void boot_run(void);

#endif /* BOOT_MAIN_H */
