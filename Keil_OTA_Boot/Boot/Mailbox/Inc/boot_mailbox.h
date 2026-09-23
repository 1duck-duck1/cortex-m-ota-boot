/**
 ******************************************************************************
 * @file    boot_mailbox.h
 * @brief   跨复位邮箱：Boot 与 App 之间仅剩的通信渠道
 *
 * SRAM 内容在复位后不会自动清零，所以这块 32 字节可以在"App 软复位 → Boot 启动"
 * 之间传递一个请求或一次确认。但它也可能残留着上电瞬间的随机值，因此整块内容
 * 靠 magic + crc32 自证：只有魔数正确且 CRC32 对得上，里面的字段才可信。
 *
 * 布局与 App 侧的 app_boot_client.c 必须逐字一致（8 个字，CRC32 覆盖前 7 个），
 * 两个工程各自编译、没有共享源文件，唯一能对齐的就是这份布局。
 ******************************************************************************
 */
#ifndef BOOT_MAILBOX_H
#define BOOT_MAILBOX_H

#include <stdbool.h>
#include <stdint.h>

/** 邮箱的落盘布局，整体 32 字节，落在 SRAM2 顶部 BOOT_MAILBOX_ADDR 处 */
typedef struct
{
    uint32_t magic;         /**< BOOT_MAILBOX_MAGIC，整块内容有效的标志 */
    uint32_t request;       /**< BOOT_MAILBOX_REQUEST 时表示 App 请求回到 Boot */
    uint32_t confirmation;  /**< BOOT_MAILBOX_CONFIRM 时表示 App 确认试运行成功 */
    uint32_t attempts;      /**< Boot 写入：本次启动是第几次试运行 */
    uint32_t heartbeat;     /**< App 写入：仅供调试观察的存活计数 */
    uint32_t launched_slot; /**< Boot 写入：本次实际启动的槽 */
    uint32_t launch_flags;  /**< Boot 写入：BOOT_MAILBOX_FLAG_PENDING 表示试运行启动 */
    uint32_t crc32;         /**< 整块 CRC32，覆盖本字段之前的 28 字节 */
} boot_mailbox_t;

/**
 * @brief  判断邮箱内容是否可信
 * @return true 表示魔数正确且 CRC32 一致
 * @note   返回值不缓存：邮箱可能被 App 在两次调用之间改写，每次都重新校验。
 */
bool boot_mailbox_is_valid(void);

/**
 * @brief  判断是否存在"请求回到 Boot"
 * @return true 表示邮箱可信且 request 字段为 BOOT_MAILBOX_REQUEST
 */
bool boot_mailbox_has_request(void);

/**
 * @brief  判断是否存在"确认试运行成功"
 * @return true 表示邮箱可信且 confirmation 字段为 BOOT_MAILBOX_CONFIRM
 */
bool boot_mailbox_has_confirm(void);

/**
 * @brief  消费掉"请求回到 Boot"
 * @note   清零并重新提交，避免同一次请求在多次复位之间反复生效。
 */
void boot_mailbox_consume_request(void);

/**
 * @brief  消费掉"确认试运行成功"
 * @note   清零并重新提交，避免同一次确认被重复计入。
 */
void boot_mailbox_consume_confirm(void);

/**
 * @brief  把邮箱填成一份干净的初始内容
 * @note   请求、确认、存活计数清零，启动槽记为 NONE。邮箱已经可信时不做任何
 *         改动——那里面可能有 App 刚写下的请求或确认。
 */
void boot_mailbox_reset(void);

/**
 * @brief  写入本次启动的结果供 App 读取
 * @param  launched_slot 本次实际启动的槽
 * @param  launch_flags  启动标志，BOOT_MAILBOX_FLAG_PENDING 表示试运行
 * @param  attempts      本次启动是第几次试运行
 * @note   跳转前调用：App 靠 launched_slot 与 launch_flags 判断自己是不是
 *         试运行镜像，从而决定要不要在自检通过后回确认。
 */
void boot_mailbox_publish(uint32_t launched_slot, uint32_t launch_flags, uint32_t attempts);

/**
 * @brief  读取 App 写入的存活计数
 * @return 存活计数；邮箱不可信时为 0
 */
uint32_t boot_mailbox_heartbeat(void);

/**
 * @brief  读取本次启动的试运行次数
 * @return 试运行次数；邮箱不可信时为 0
 */
uint32_t boot_mailbox_attempts(void);

/**
 * @brief  读取本次实际启动的槽
 * @return 槽编号；邮箱不可信时为 BOOT_SLOT_ID_NONE
 */
uint32_t boot_mailbox_launched_slot(void);

/**
 * @brief  读取本次启动的标志位
 * @return 标志位；邮箱不可信时为 0
 */
uint32_t boot_mailbox_launch_flags(void);

#endif /* BOOT_MAILBOX_H */
