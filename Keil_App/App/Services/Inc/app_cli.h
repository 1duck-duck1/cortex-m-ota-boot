/**
 ******************************************************************************
 * @file    app_cli.h
 * @brief   App 侧命令行：info / slot / ota recv|erase|confirm / boot / reset
 *
 * 命令表在 App 侧注册（Boot 只注册恢复所需的最小集），解析与响应格式实现同源于
 * `Common/ota/ota_cli.c`。命令行是升级的唯一入口，YMODEM 是它的子命令
 * （docs/04-boot-implementation.md §8.6）。
 ******************************************************************************
 */
#ifndef APP_CLI_H
#define APP_CLI_H

#include "app_ota_service.h"

/**
 * @brief  泵一次命令行：取走环形缓冲里已到达的字节并解析
 * @note   必须在主循环里反复调用。首次调用时若串口已就绪，会初始化命令行并
 *         发一行版本横幅；升级会话进行期间调用方**不得**调用本函数——YMODEM
 *         的二进制流会把行缓冲冲乱（与 Boot 侧同一条规则）。
 */
void app_cli_poll(void);

/**
 * @brief  输出一次升级会话的结果
 * @param  result app_ota_service_run() 的返回值，调用方只在它不为 BUSY 时调用
 * @note   成功输出 `OK ota=<槽> ver=.. size=.. crc32=..`；失败输出
 *         `ERR <code> reason=<原因>`，便于脚本解析与人工确认。
 */
void app_cli_report_ota(app_ota_result_t result);

#endif /* APP_CLI_H */
