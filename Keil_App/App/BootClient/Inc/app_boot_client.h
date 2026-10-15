/**
 ******************************************************************************
 * @file    app_boot_client.h
 * @brief   应用程序与 Bootloader 之间的跨复位通信接口
 ******************************************************************************
 */
#ifndef APP_BOOT_CLIENT_H
#define APP_BOOT_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 初始化应用向量表，使其指向当前编译槽位。 */
void app_boot_client_init(void);

/** @brief 通知 Bootloader 当前 Pending 固件已经通过应用自检。 */
void app_boot_client_confirm(void);

/** @brief 当前 App 是否由 Boot 作为 Pending 镜像启动。 */
bool app_boot_client_is_pending(void);

/** @brief 确认 Pending 固件并复位，由 Boot 将确认结果持久化。 */
void app_boot_client_confirm_and_reset(void);

/** @brief 请求进入 Bootloader，并立即触发软件复位。 */
void app_boot_client_request_boot(void);

/** @brief 更新仅供调试观察的应用存活计数。 */
void app_boot_client_heartbeat(void);

#endif /* APP_BOOT_CLIENT_H */
