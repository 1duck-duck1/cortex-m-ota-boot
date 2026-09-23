/**
 ******************************************************************************
 * @file    app_cli.c
 * @brief   App 侧命令行实现
 ******************************************************************************
 */
#include "app_cli.h"

#include "app_boot_client.h"
#include "app_config.h"
#include "app_flash.h"
#include "app_transport.h"
#include "ota_cli.h"

#include "stm32f4xx_hal.h"

#include <string.h>

/** 单轮最多处理的字节数：保证收命令期间升级状态机仍能按时推进 */
#define APP_CLI_READ_CHUNK      32u

static ota_cli_t s_cli;
static bool s_is_started;

static void app_cli_write(const char *text, uint32_t size, void *write_context)
{
    (void)write_context;
    /* 响应必须同步发完：命令处理函数返回后框架认为该行已在线上
       （`reset` / `boot` / `ota confirm` 都依赖它）。 */
    (void)app_transport_write((const uint8_t *)text, size);
}

static const char *app_cli_slot_name(uint32_t slot_id)
{
    if (slot_id == APP_SLOT_ID_A)
    {
        return "A";
    }
    if (slot_id == APP_SLOT_ID_B)
    {
        return "B";
    }
    return "NONE";
}

static const char *app_cli_image_state_name(uint32_t slot_id)
{
    return app_ota_service_slot_has_image(slot_id) ? "VALID" : "EMPTY";
}

/**
 * @brief  解析槽位字母
 * @param  text    文本，必须是单个 'A'/'B'（大小写皆可）
 * @param  slot_id 解析结果
 * @return true 表示解析成功
 */
static bool app_cli_parse_slot(const char *text, uint32_t *slot_id)
{
    if ((text == NULL) || (slot_id == NULL) || (text[1] != '\0'))
    {
        return false;
    }

    if ((text[0] == 'A') || (text[0] == 'a'))
    {
        *slot_id = APP_SLOT_ID_A;
        return true;
    }
    if ((text[0] == 'B') || (text[0] == 'b'))
    {
        *slot_id = APP_SLOT_ID_B;
        return true;
    }

    return false;
}

static ota_cli_result_t app_cli_cmd_info(int argc, char *argv[],
                                        void *handler_context)
{
    (void)argv;
    (void)handler_context;

    if (argc != 1)
    {
        return OTA_CLI_ERR_BAD_PARAM;
    }

    ota_cli_reply_ok_begin(&s_cli);
    ota_cli_reply_kv_str(&s_cli, "ver", APP_VERSION_STRING);
    ota_cli_reply_kv_str(&s_cli, "run", app_cli_slot_name(APP_SLOT_ID));
    ota_cli_reply_kv_u32(&s_cli, "pending",
                         app_boot_client_is_pending() ? 1u : 0u);
    ota_cli_reply_kv_str(&s_cli, "a_state",
                         app_cli_image_state_name(APP_SLOT_ID_A));
    ota_cli_reply_kv_str(&s_cli, "b_state",
                         app_cli_image_state_name(APP_SLOT_ID_B));
    ota_cli_reply_kv_u32(&s_cli, "rx_overrun",
                         app_transport_has_overrun() ? 1u : 0u);
    ota_cli_reply_end(&s_cli);

    return OTA_CLI_OK;
}

/**
 * @brief  `slot`：报告两槽状态
 * @note   App 读不到 Boot 的 Metadata，因此只能给出"有没有看起来可启动的镜像"
 *         （槽首向量表 SP/PC 预检），报不出长度与 CRC32——那两项只有持有
 *         Metadata 的 Boot 侧 `slot` 命令能给。偏差记在
 *         docs/04-boot-implementation.md §8.6。
 */
static ota_cli_result_t app_cli_cmd_slot(int argc, char *argv[],
                                        void *handler_context)
{
    (void)argv;
    (void)handler_context;

    if (argc != 1)
    {
        return OTA_CLI_ERR_BAD_PARAM;
    }

    ota_cli_reply_ok_begin(&s_cli);
    ota_cli_reply_kv_str(&s_cli, "run", app_cli_slot_name(APP_SLOT_ID));
    ota_cli_reply_kv_str(&s_cli, "a_state",
                         app_cli_image_state_name(APP_SLOT_ID_A));
    ota_cli_reply_kv_str(&s_cli, "b_state",
                         app_cli_image_state_name(APP_SLOT_ID_B));
    ota_cli_reply_end(&s_cli);

    return OTA_CLI_OK;
}

/**
 * @brief  `ota erase A|B`：擦除非运行槽
 * @note   擦 384 KB 期间 CPU 被 Flash 总线 stall，响应会在擦完之后才发出。
 *         拒绝擦运行槽：那等于立刻砖化，App 自己是最后一道防线。
 */
static ota_cli_result_t app_cli_ota_erase(const char *text)
{
    uint32_t slot_id;

    if (!app_cli_parse_slot(text, &slot_id))
    {
        return OTA_CLI_ERR_BAD_PARAM;
    }
    if (slot_id == APP_SLOT_ID)
    {
        ota_cli_reply_err_begin(&s_cli, OTA_CLI_ERR_BAD_PARAM);
        ota_cli_reply_kv_str(&s_cli, "reason", "RUNNING_SLOT");
        ota_cli_reply_end(&s_cli);
        return OTA_CLI_OK;
    }
    if (!app_flash_erase_slot(slot_id))
    {
        ota_cli_reply_err_begin(&s_cli, OTA_CLI_ERR_FAILED);
        ota_cli_reply_kv_str(&s_cli, "reason", "FLASH_FAILED");
        ota_cli_reply_end(&s_cli);
        return OTA_CLI_OK;
    }

    ota_cli_reply_ok_begin(&s_cli);
    ota_cli_reply_kv_str(&s_cli, "erased", app_cli_slot_name(slot_id));
    ota_cli_reply_end(&s_cli);

    return OTA_CLI_OK;
}

/**
 * @brief  `ota confirm`：手工确认 Pending 固件并复位
 * @note   正常路径下不会走到这里：main.c 在启动自检通过后就自动确认并复位，
 *         等命令行跑起来时邮箱里已经没有 Pending 了。本命令作为自动确认失效时
 *         的兜底保留。
 * @note   只在本次由 Boot 作为 Pending 镜像启动时有效：其它情况下写邮箱不会
 *         改变任何状态（Boot 的确认分支要求 pending 槽与本次启动槽匹配），
 *         因此直接拒绝并说明原因，而不是发一条"看起来成功"的 OK。
 */
static ota_cli_result_t app_cli_ota_confirm(void)
{
    if (!app_boot_client_is_pending())
    {
        ota_cli_reply_err_begin(&s_cli, OTA_CLI_ERR_FAILED);
        ota_cli_reply_kv_str(&s_cli, "reason", "NOT_PENDING");
        ota_cli_reply_end(&s_cli);
        return OTA_CLI_OK;
    }

    ota_cli_reply_ok_begin(&s_cli);
    ota_cli_reply_kv_str(&s_cli, "confirm", app_cli_slot_name(APP_SLOT_ID));
    ota_cli_reply_end(&s_cli);

    /* 先关串口再复位：避免复位瞬间线上留下半帧，上位机把它误判成命令失败。 */
    app_transport_deinit();
    app_boot_client_confirm_and_reset();

    return OTA_CLI_OK;
}

/**
 * @brief  `ota recv [version]`：登记一次升级请求
 * @note   与 Boot 侧同一条规则——只登记不启动，且不回 OK 行：紧接线上跑的是
 *         YMODEM 握手，结果由 app_ota_service_run() 结束后统一报告。立刻开跑会
 *         与同一批字节的后续解析抢数据。
 */
static ota_cli_result_t app_cli_ota_recv(int argc, char *argv[])
{
    uint32_t version = 0u;
    bool has_version = false;

    if (argc == 3)
    {
        if (!ota_cli_parse_version(argv[2], &version))
        {
            return OTA_CLI_ERR_BAD_PARAM;
        }
        has_version = true;
    }

    app_ota_service_request(version, has_version);

    return OTA_CLI_OK;
}

static ota_cli_result_t app_cli_cmd_ota(int argc, char *argv[],
                                       void *handler_context)
{
    (void)handler_context;

    if (argc < 2)
    {
        return OTA_CLI_ERR_BAD_PARAM;
    }

    if (strcmp(argv[1], "recv") == 0)
    {
        if (argc > 3)
        {
            return OTA_CLI_ERR_BAD_PARAM;
        }
        return app_cli_ota_recv(argc, argv);
    }

    if (strcmp(argv[1], "erase") == 0)
    {
        if (argc != 3)
        {
            return OTA_CLI_ERR_BAD_PARAM;
        }
        return app_cli_ota_erase(argv[2]);
    }

    if (strcmp(argv[1], "confirm") == 0)
    {
        if (argc != 2)
        {
            return OTA_CLI_ERR_BAD_PARAM;
        }
        return app_cli_ota_confirm();
    }

    return OTA_CLI_ERR_UNKNOWN_COMMAND;
}

/**
 * @brief  `boot`：请求回跳到 Bootloader
 */
static ota_cli_result_t app_cli_cmd_boot(int argc, char *argv[],
                                        void *handler_context)
{
    (void)argv;
    (void)handler_context;

    if (argc != 1)
    {
        return OTA_CLI_ERR_BAD_PARAM;
    }

    ota_cli_reply_ok_begin(&s_cli);
    ota_cli_reply_end(&s_cli);

    app_transport_deinit();
    app_boot_client_request_boot();

    return OTA_CLI_OK;
}

static ota_cli_result_t app_cli_cmd_reset(int argc, char *argv[],
                                         void *handler_context)
{
    (void)argv;
    (void)handler_context;

    if (argc != 1)
    {
        return OTA_CLI_ERR_BAD_PARAM;
    }

    ota_cli_reply_ok_begin(&s_cli);
    ota_cli_reply_end(&s_cli);

    app_transport_deinit();
    NVIC_SystemReset();

    return OTA_CLI_OK;
}

static const ota_cli_command_t s_app_commands[] =
{
    { "info",  "info",                       app_cli_cmd_info },
    { "slot",  "slot",                       app_cli_cmd_slot },
    { "ota",   "ota recv|erase|confirm",     app_cli_cmd_ota },
    { "boot",  "boot",                       app_cli_cmd_boot },
    { "reset", "reset",                      app_cli_cmd_reset },
};

static void app_cli_send_banner(void)
{
    /* 上电先报一行版本：不用猜命令就能确认波特率与链路是否正常。 */
    ota_cli_reply_ok_begin(&s_cli);
    ota_cli_reply_kv_str(&s_cli, "app", APP_VERSION_STRING);
    ota_cli_reply_kv_str(&s_cli, "run", app_cli_slot_name(APP_SLOT_ID));
    ota_cli_reply_end(&s_cli);
}

void app_cli_report_ota(app_ota_result_t result)
{
    const app_ota_status_t *status = app_ota_service_status();

    if (result == APP_OTA_DONE)
    {
        /* 走到这里 NVS 请求已落盘，调用方随即复位回 Boot。size 与 crc32 一并
           回报，上位机可用 zlib.crc32 对照，确认整条链路与落盘都无误。 */
        ota_cli_reply_ok_begin(&s_cli);
        ota_cli_reply_kv_str(&s_cli, "ota",
                             app_cli_slot_name(status->target_slot));
        ota_cli_reply_kv_u32(&s_cli, "ver", status->version);
        ota_cli_reply_kv_u32(&s_cli, "size", status->size);
        ota_cli_reply_kv_u32(&s_cli, "crc32", status->crc32);
        ota_cli_reply_end(&s_cli);
        return;
    }

    /* 失败一律回 ERR：ERR 2 = 设备不接受这份声明，ERR 3 = 链路或对端行为异常，
       具体原因放在 reason 字段里，便于区分 CAN / 超时 / 重传超限。 */
    ota_cli_reply_err_begin(&s_cli,
                            (result == APP_OTA_REJECTED) ?
                            OTA_CLI_ERR_BAD_PARAM : OTA_CLI_ERR_FAILED);
    ota_cli_reply_kv_str(&s_cli, "reason",
                         app_ota_reason_name(status->reason));
    ota_cli_reply_end(&s_cli);
}

void app_cli_poll(void)
{
    uint8_t chunk[APP_CLI_READ_CHUNK];
    uint32_t count;

    if (!s_is_started)
    {
        if (!app_transport_is_ready())
        {
            /* 串口没起来不该把命令行判死：下一轮再试。 */
            return;
        }

        ota_cli_init(&s_cli, s_app_commands,
                     (uint32_t)(sizeof(s_app_commands) / sizeof(s_app_commands[0])),
                     NULL, app_cli_write, NULL);
        s_is_started = true;
        app_cli_send_banner();
    }

    count = app_transport_read(chunk, (uint32_t)sizeof(chunk));
    if (count > 0u)
    {
        ota_cli_feed(&s_cli, chunk, count);
    }
}
