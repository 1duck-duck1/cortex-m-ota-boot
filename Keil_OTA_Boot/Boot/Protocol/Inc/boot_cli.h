/**
 ******************************************************************************
 * @file    boot_cli.h
 * @brief   Boot 侧命令表：状态查询、启动升级、补镜像头、复位
 *
 * 命令行是升级的唯一入口。命令词只有四个：info、slot、ota、reset，其中 ota
 * 带两个子命令。刻意不做 flash 擦写、地址读写之类的"万能命令"——一条敲错的
 * erase 就能把 Boot 自己抹掉，而这类操作在开发阶段用调试器做更顺手。
 *
 * 与 Common/ota 的框架分工：框架负责行编辑、切词、应答格式，本模块只注册
 * 命令表并提供业务上下文。
 ******************************************************************************
 */
#ifndef BOOT_CLI_H
#define BOOT_CLI_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_metadata.h"
#include "boot_types.h"
#include "boot_update.h"
#include "ota_cli.h"

/** 命令处理函数共享的上下文 */
typedef struct
{
    ota_cli_t *cli;                 /**< 命令行实例，用于组装应答 */
    boot_metadata_t *metadata;      /**< 当前 Metadata */
    boot_update_t *update;          /**< OTA 会话对象 */
    const boot_status_t *status;    /**< 启动决策快照，供 info 展示 */
    uint32_t target_slot;           /**< 本次升级的目标槽，由启动决策推出 */
    uint32_t protected_slot;        /**< 受保护槽，不允许被覆盖 */
} boot_cli_context_t;

/**
 * @brief  初始化命令行实例并注册命令表
 * @param  cli     待初始化的命令行实例，不可为 NULL
 * @param  context 业务上下文，必须是静态存储期，不可为 NULL
 * @note   命令表是静态的，只记录指针不复制。
 */
void boot_cli_init(ota_cli_t *cli, boot_cli_context_t *context);

/**
 * @brief  命令行与回显的写出函数
 * @param  text          待写出的字节
 * @param  size          字节数
 * @param  write_context 未使用
 * @note   同步阻塞写出，保证命令处理函数返回时该行已在线上。
 */
void boot_cli_write(const char *text, uint32_t size, void *write_context);

#endif /* BOOT_CLI_H */
