/**
 ******************************************************************************
 * @file    boot_cli.c
 * @brief   Boot 侧命令表的实现
 ******************************************************************************
 */
#include "boot_cli.h"

#include "boot_adopt.h"
#include "boot_conf.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_jump.h"
#include "boot_mailbox.h"
#include "boot_nvs.h"
#include "boot_transport.h"

#include <string.h>

/**
 * @brief  把槽编号转成单字母，供应答输出
 * @param  slot_id 槽编号
 * @return "A" / "B" / "NONE"
 */
static const char *boot_cli_slot_name(uint32_t slot_id)
{
    if (slot_id == BOOT_SLOT_ID_A) {
        return "A";
    }
    if (slot_id == BOOT_SLOT_ID_B) {
        return "B";
    }

    return "NONE";
}

/**
 * @brief  把槽单字母解析成槽编号
 * @param  text    待解析文本
 * @param  slot_id 解析结果
 * @return true 表示文本是 "A" / "a" / "B" / "b"
 */
static bool boot_cli_parse_slot(const char *text, uint32_t *slot_id)
{
    if ((text == NULL) || (slot_id == NULL) || (text[1] != '\0')) {
        return false;
    }

    if ((text[0] == 'A') || (text[0] == 'a')) {
        *slot_id = BOOT_SLOT_ID_A;
        return true;
    }
    if ((text[0] == 'B') || (text[0] == 'b')) {
        *slot_id = BOOT_SLOT_ID_B;
        return true;
    }

    return false;
}

/**
 * @brief  输出一个槽的镜像状态
 * @param  context 业务上下文
 * @param  slot_id 槽编号
 * @note   展示的是逐槽校验的完整结论，包含载荷长度与 CRC32；校验本身要读完
 *         整个镜像，因此这是条"有代价"的查询命令，不会放进启动路径。
 */
static void boot_cli_report_slot(boot_cli_context_t *context, uint32_t slot_id)
{
    boot_image_t image;
    boot_image_state_t state = boot_image_check(slot_id, &image);

    ota_cli_t *cli = context->cli;

    ota_cli_reply_ok_begin(cli);
    ota_cli_reply_kv_str(cli, "slot", boot_cli_slot_name(slot_id));
    ota_cli_reply_kv_str(cli, "state", boot_image_state_name(state));
    ota_cli_reply_kv_u32(cli, "size", image.image_size);
    ota_cli_reply_kv_hex(cli, "crc", image.payload_crc32);
    ota_cli_reply_kv_hex(cli, "ver", image.version);
    ota_cli_reply_end(cli);
}

/**
 * @brief  info 命令：Boot 自身状态与启动决策快照
 * @param  argc 参数个数
 * @param  argv 参数数组
 * @param  ctx  业务上下文
 * @return OTA_CLI_OK
 */
static ota_cli_result_t boot_cli_info(int argc, char *argv[], void *ctx)
{
    boot_cli_context_t *context = (boot_cli_context_t *)ctx;
    ota_cli_t *cli = context->cli;
    const boot_status_t *status = context->status;
    ota_request_t request;
    bool has_request = boot_nvs_read(&request);

    (void)argc;
    (void)argv;

    ota_cli_reply_ok_begin(cli);
    ota_cli_reply_kv_str(cli, "boot", BOOT_VERSION_STRING);
    ota_cli_reply_kv_str(cli, "meta", status->is_metadata_valid ? "valid" : "default");
    ota_cli_reply_kv_str(cli, "active", boot_cli_slot_name(status->active_slot));
    ota_cli_reply_kv_str(cli, "pending", boot_cli_slot_name(status->pending_slot));
    ota_cli_reply_kv_u32(cli, "attempts", status->boot_attempts);
    ota_cli_reply_kv_u32(cli, "max", status->max_attempts);
    ota_cli_reply_end(cli);

    ota_cli_reply_ok_begin(cli);
    ota_cli_reply_kv_str(cli, "running", boot_cli_slot_name(status->running_slot));
    ota_cli_reply_kv_u32(cli, "heartbeat", boot_mailbox_heartbeat());
    ota_cli_reply_kv_str(cli, "next", boot_cli_slot_name(context->target_slot));
    ota_cli_reply_end(cli);

    ota_cli_reply_ok_begin(cli);
    ota_cli_reply_kv_str(cli, "ota_req", has_request ? "present" : "none");
    ota_cli_reply_kv_str(cli, "ota_slot", has_request ?
                         boot_cli_slot_name(request.target_slot) : "NONE");
    ota_cli_reply_kv_u32(cli, "ota_size", has_request ? request.image_size : 0u);
    ota_cli_reply_kv_hex(cli, "ota_ver", has_request ? request.version : 0u);
    ota_cli_reply_kv_str(cli, "ota_sig", has_request ?
                         ((request.sig_algorithm == OTA_IMAGE_SIG_NONE) ? "NO" : "YES") : "none");
    ota_cli_reply_kv_hex(cli, "ota_kid", has_request ? request.pubkey_id : 0u);
    ota_cli_reply_end(cli);

    return OTA_CLI_OK;
}

/**
 * @brief  slot 命令：逐个槽的镜像状态
 * @param  argc 参数个数
 * @param  argv 参数数组
 * @param  ctx  业务上下文
 * @return OTA_CLI_OK
 */
static ota_cli_result_t boot_cli_slot(int argc, char *argv[], void *ctx)
{
    boot_cli_context_t *context = (boot_cli_context_t *)ctx;

    (void)argc;
    (void)argv;

    boot_cli_report_slot(context, BOOT_SLOT_ID_A);
    boot_cli_report_slot(context, BOOT_SLOT_ID_B);

    return OTA_CLI_OK;
}

/**
 * @brief  ota recv 子命令：登记一次接收会话
 * @param  argc 参数个数
 * @param  argv 参数数组
 * @param  ctx  业务上下文
 * @return 处理结果
 * @note   本命令只登记，不等待结果；会话结束后由主循环输出一行结论。这样上位机
 *         在收到命令应答后可以立刻开始发送，不给 YMODEM 握手留空档。
 */
static ota_cli_result_t boot_cli_ota_recv(int argc, char *argv[], void *ctx)
{
    boot_cli_context_t *context = (boot_cli_context_t *)ctx;
    uint32_t version = OTA_REQUEST_VERSION_NONE;
    bool has_version = false;

    if (argc > 3) {
        return OTA_CLI_ERR_BAD_PARAM;
    }
    if (argc == 3) {
        if (!ota_cli_parse_version(argv[2], &version)) {
            return OTA_CLI_ERR_BAD_PARAM;
        }
        has_version = true;
    }

    if (!boot_update_start(context->update,
                           context->target_slot,
                           context->protected_slot,
                           version,
                           has_version)) {
        ota_cli_reply_err_begin(context->cli, OTA_CLI_ERR_FAILED);
        ota_cli_reply_kv_str(context->cli, "reason", "BUSY");
        ota_cli_reply_end(context->cli);
        return OTA_CLI_OK;
    }

    ota_cli_reply_ok_begin(context->cli);
    ota_cli_reply_kv_str(context->cli, "ota", "recv");
    ota_cli_reply_kv_str(context->cli, "slot", boot_cli_slot_name(context->target_slot));
    if (has_version) {
        ota_cli_reply_kv_hex(context->cli, "ver", version);
    }
    ota_cli_reply_end(context->cli);

    return OTA_CLI_OK;
}

/**
 * @brief  ota adopt 子命令：给 Keil 直接烧录的槽补镜像头
 * @param  argc 参数个数
 * @param  argv 参数数组
 * @param  ctx  业务上下文
 * @return 处理结果
 * @note   只在升级模式下可用——本命令所在的主循环本来就只有升级模式会跑，
 *         不存在被正常启动路径调用的可能。
 */
static ota_cli_result_t boot_cli_ota_adopt(int argc, char *argv[], void *ctx)
{
    boot_cli_context_t *context = (boot_cli_context_t *)ctx;
    boot_adopt_report_t report;
    uint32_t slot_id = BOOT_SLOT_ID_NONE;
    uint32_t version = OTA_REQUEST_VERSION_NONE;

    if (argc != 4) {
        return OTA_CLI_ERR_BAD_PARAM;
    }
    if (!boot_cli_parse_slot(argv[2], &slot_id)) {
        return OTA_CLI_ERR_BAD_PARAM;
    }
    if (!ota_cli_parse_version(argv[3], &version)) {
        return OTA_CLI_ERR_BAD_PARAM;
    }

    if (boot_adopt_slot(slot_id,
                        context->protected_slot,
                        version,
                        context->metadata,
                        &report) != BOOT_ADOPT_OK) {
        ota_cli_reply_err_begin(context->cli, OTA_CLI_ERR_FAILED);
        ota_cli_reply_kv_str(context->cli, "reason", boot_adopt_result_name(report.result));
        ota_cli_reply_end(context->cli);
        return OTA_CLI_OK;
    }

    ota_cli_reply_ok_begin(context->cli);
    ota_cli_reply_kv_str(context->cli, "ota", "adopt");
    ota_cli_reply_kv_str(context->cli, "slot", boot_cli_slot_name(slot_id));
    ota_cli_reply_kv_u32(context->cli, "size", report.image_size);
    ota_cli_reply_kv_hex(context->cli, "crc", report.image_crc32);
    ota_cli_reply_end(context->cli);

    return OTA_CLI_OK;
}

/**
 * @brief  ota 命令：分发 recv 与 adopt 子命令
 * @param  argc 参数个数
 * @param  argv 参数数组
 * @param  ctx  业务上下文
 * @return 处理结果
 */
static ota_cli_result_t boot_cli_ota(int argc, char *argv[], void *ctx)
{
    if (argc < 2) {
        return OTA_CLI_ERR_BAD_PARAM;
    }

    if (strcmp(argv[1], "recv") == 0) {
        return boot_cli_ota_recv(argc, argv, ctx);
    }
    if (strcmp(argv[1], "adopt") == 0) {
        return boot_cli_ota_adopt(argc, argv, ctx);
    }

    return OTA_CLI_ERR_BAD_PARAM;
}

/**
 * @brief  reset 命令：软复位回到启动决策
 * @param  argc 参数个数
 * @param  argv 参数数组
 * @param  ctx  业务上下文
 * @return OTA_CLI_OK
 */
static ota_cli_result_t boot_cli_reset(int argc, char *argv[], void *ctx)
{
    boot_cli_context_t *context = (boot_cli_context_t *)ctx;

    (void)argc;
    (void)argv;

    ota_cli_reply_ok_begin(context->cli);
    ota_cli_reply_kv_str(context->cli, "action", "reset");
    ota_cli_reply_end(context->cli);

    boot_jump_restart();

    return OTA_CLI_OK;
}

static const ota_cli_command_t s_commands[] =
{
    { "info",  "info",                                  boot_cli_info },
    { "slot",  "slot",                                  boot_cli_slot },
    { "ota",   "ota recv [0xVERSION] | ota adopt A|B 0xVERSION", boot_cli_ota },
    { "reset", "reset",                                 boot_cli_reset }
};

void boot_cli_write(const char *text, uint32_t size, void *write_context)
{
    (void)write_context;

    (void)boot_transport_write((const uint8_t *)text, size);
}

void boot_cli_init(ota_cli_t *cli, boot_cli_context_t *context)
{
    ota_cli_init(cli,
                 s_commands,
                 (uint32_t)(sizeof(s_commands) / sizeof(s_commands[0])),
                 context,
                 boot_cli_write,
                 NULL);
}
