/**
 ******************************************************************************
 * @file    boot_main.c
 * @brief   Boot 启动流程的实现
 ******************************************************************************
 */
#include "boot_main.h"

#include <string.h>

#include "boot_cli.h"
#include "boot_conf.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_jump.h"
#include "boot_mailbox.h"
#include "boot_nvs.h"
#include "boot_transport.h"
#include "boot_types.h"
#include "boot_update.h"
#include "ota_cli.h"

/* 这些都是"整个升级模式生命周期都要存在"的对象，放静态区：update 里的
   YMODEM 组帧缓冲有 1 KB，cli 的行缓冲与应答缓冲加起来接近 300 字节，
   放栈上会把 Boot 那点栈挤爆。 */
static boot_metadata_t s_metadata;
static boot_status_t s_status;
static boot_update_t s_update;
static ota_cli_t s_cli;
static boot_cli_context_t s_cli_context;

/** 决策选中的那份镜像的校验结果，供跳转时直接取用，避免再算一遍载荷 CRC32 */
static boot_image_t s_chosen;

/**
 * @brief  取受保护槽：不允许被升级覆盖的那一个
 * @param  metadata     当前 Metadata
 * @param  running_slot 本次决策选中的槽
 * @return 受保护槽；没有需要保护的槽时返回 BOOT_SLOT_ID_NONE
 * @note   正在跑的槽当然要保护。停在升级模式（没选中任何槽）时，只要 active
 *         槽里确实还有一份能启动的镜像，也同样保护它——否则一次误操作就会把
 *         设备唯一能跑的东西擦掉。
 */
static uint32_t boot_main_protected_slot(const boot_metadata_t *metadata, uint32_t running_slot)
{
    if (running_slot != BOOT_SLOT_ID_NONE)
    {
        return running_slot;
    }
    if (metadata->active_slot == BOOT_SLOT_ID_NONE)
    {
        return BOOT_SLOT_ID_NONE;
    }
    if (boot_image_check(metadata->active_slot, NULL) == BOOT_IMAGE_VALID)
    {
        return metadata->active_slot;
    }

    return BOOT_SLOT_ID_NONE;
}

/**
 * @brief  取本次升级的目标槽
 * @param  metadata       当前 Metadata
 * @param  protected_slot 受保护槽
 * @return 目标槽编号
 * @note   目标槽由设备自己推导，不需要上位机指定：正常情况下就是"另一个槽"。
 *         连 active 都没有时（两槽都没装好），挑一个当前跑不起来的槽来覆盖。
 */
static uint32_t boot_main_choose_target(const boot_metadata_t *metadata, uint32_t protected_slot)
{
    uint32_t active = metadata->active_slot;

    if ((active == BOOT_SLOT_ID_A) || (active == BOOT_SLOT_ID_B))
    {
        return boot_flash_other_slot(active);
    }

    if ((protected_slot != BOOT_SLOT_ID_A) &&
        (boot_image_check(BOOT_SLOT_ID_A, NULL) != BOOT_IMAGE_VALID))
    {
        return BOOT_SLOT_ID_A;
    }
    if ((protected_slot != BOOT_SLOT_ID_B) &&
        (boot_image_check(BOOT_SLOT_ID_B, NULL) != BOOT_IMAGE_VALID))
    {
        return BOOT_SLOT_ID_B;
    }

    return BOOT_SLOT_ID_A;
}

/**
 * @brief  消费邮箱里的确认，把 pending 槽转正
 * @param  metadata 当前 Metadata
 * @return true 表示确认已生效并落盘
 * @note   只有邮箱记录的启动槽与 Metadata 的 pending 槽一致时才认这次确认：
 *         否则一份陈旧的 CONFIRM 会把一个从未试运行过的槽直接扶正。
 */
static bool boot_main_handle_confirm(boot_metadata_t *metadata)
{
    if (!boot_mailbox_has_confirm())
    {
        return false;
    }
    if (boot_mailbox_launched_slot() != metadata->pending_slot)
    {
        boot_mailbox_consume_confirm();
        return false;
    }

    boot_mailbox_consume_confirm();

    if (!boot_metadata_confirm(metadata))
    {
        return false;
    }

    return boot_metadata_save(metadata);
}

/**
 * @brief  启动决策：先看 pending，再看 active，最后自愈
 * @param  metadata 当前 Metadata
 * @param  status   决策快照
 * @note   pending 走"试运行 + 确认"：每次试运行前把计数加一，计数用尽就回滚。
 *         计数在跳转前才提交，所以进升级模式绕一圈不会白白消耗一次试运行机会。
 */
static void boot_main_decide(boot_metadata_t *metadata, boot_status_t *status)
{
    boot_image_t image;
    uint32_t pending = metadata->pending_slot;
    uint32_t active = metadata->active_slot;
    uint32_t candidate = BOOT_SLOT_ID_NONE;
    uint32_t other = BOOT_SLOT_ID_NONE;

    status->active_slot = active;
    status->pending_slot = pending;
    status->boot_attempts = metadata->boot_attempts;
    status->max_attempts = metadata->max_attempts;
    status->running_slot = BOOT_SLOT_ID_NONE;
    status->is_pending_launch = false;

    if ((pending == BOOT_SLOT_ID_A) || (pending == BOOT_SLOT_ID_B))
    {
        if (boot_image_check(pending, &image) != BOOT_IMAGE_VALID)
        {
            /* 还没试运行就发现镜像坏了，直接作废，回到 active。 */
            (void)boot_metadata_clear_pending(metadata);
            (void)boot_metadata_save(metadata);
            pending = BOOT_SLOT_ID_NONE;
        }
        else if (metadata->boot_attempts >= metadata->max_attempts)
        {
            /* 试运行次数用尽仍未收到确认，判定新镜像不可用并回滚。 */
            (void)boot_metadata_clear_pending(metadata);
            (void)boot_metadata_save(metadata);
            pending = BOOT_SLOT_ID_NONE;
        }
        else
        {
            s_chosen = image;
            status->decision = BOOT_DECISION_RUN_PENDING;
            status->running_slot = pending;
            status->is_pending_launch = true;
            return;
        }
    }

    status->pending_slot = BOOT_SLOT_ID_NONE;

    if (((active == BOOT_SLOT_ID_A) || (active == BOOT_SLOT_ID_B)) &&
        (boot_image_check(active, &image) == BOOT_IMAGE_VALID))
    {
        s_chosen = image;
        status->decision = BOOT_DECISION_RUN_ACTIVE;
        status->running_slot = active;
        return;
    }

    /* active 坏了或压根没有：看看另一槽能不能救场。 */
    if ((active == BOOT_SLOT_ID_A) || (active == BOOT_SLOT_ID_B))
    {
        other = boot_flash_other_slot(active);
        if (boot_image_check(other, &image) == BOOT_IMAGE_VALID)
        {
            candidate = other;
        }
    }
    else
    {
        if (boot_image_check(BOOT_SLOT_ID_A, &image) == BOOT_IMAGE_VALID)
        {
            candidate = BOOT_SLOT_ID_A;
        }
        else if (boot_image_check(BOOT_SLOT_ID_B, &image) == BOOT_IMAGE_VALID)
        {
            candidate = BOOT_SLOT_ID_B;
        }
    }

    if (candidate != BOOT_SLOT_ID_NONE)
    {
        metadata->active_slot = candidate;
        metadata->pending_slot = BOOT_SLOT_ID_NONE;
        metadata->boot_attempts = 0u;
        (void)boot_metadata_save(metadata);

        s_chosen = image;
        status->active_slot = candidate;
        status->decision = BOOT_DECISION_RUN_RECOVER;
        status->running_slot = candidate;
        return;
    }

    /* 两个槽都没有可启动的镜像，只能停下来等升级。 */
    status->decision = BOOT_DECISION_WAIT_UPDATE;
}

/**
 * @brief  输出一次接收会话的最终结论
 * @note   DONE 会立刻软复位，让决策流程重新走一遍并选中新槽；FAILED 则把会话
 *         恢复成空闲，让用户可以原地重试。
 */
static void boot_main_poll_session(void)
{
    boot_update_state_t state = boot_update_state(&s_update);

    if (state == BOOT_UPDATE_STATE_DONE)
    {
        ota_cli_reply_ok_begin(&s_cli);
        ota_cli_reply_kv_str(&s_cli, "ota", "done");
        ota_cli_reply_kv_str(&s_cli, "slot",
                             (s_update.target_slot == BOOT_SLOT_ID_A) ? "A" : "B");
        ota_cli_reply_kv_u32(&s_cli, "size", s_update.image_size);
        ota_cli_reply_kv_hex(&s_cli, "crc", s_update.image_crc32);
        ota_cli_reply_end(&s_cli);

        boot_jump_restart();
    }

    if (state == BOOT_UPDATE_STATE_FAILED)
    {
        ota_cli_reply_err_begin(&s_cli, OTA_CLI_ERR_FAILED);
        ota_cli_reply_kv_str(&s_cli, "ota", "recv");
        ota_cli_reply_kv_str(&s_cli, "reason", boot_update_reason_name(s_update.reason));
        ota_cli_reply_end(&s_cli);

        boot_update_init(&s_update, &s_metadata);
    }
}

/**
 * @brief  进入升级模式，停在命令行主循环
 * @param  status 决策快照
 * @note   本函数永不返回。串口起不来时没有别的出路：没有链路就没有升级手段，
 *         只能停住等复位，好在 Boot 本身还在，下一次复位仍会重新决策。
 */
static void boot_main_run_update_mode(const boot_status_t *status)
{
    uint8_t byte;

    if (!boot_transport_init())
    {
        for (;;)
        {
        }
    }

    boot_mailbox_publish(BOOT_SLOT_ID_NONE, 0u, status->boot_attempts);

    s_cli_context.cli = &s_cli;
    s_cli_context.metadata = &s_metadata;
    s_cli_context.update = &s_update;
    s_cli_context.status = status;

    boot_cli_init(&s_cli, &s_cli_context);
    boot_update_init(&s_update, &s_metadata);

    boot_transport_write_str("\r\n[BOOT] " BOOT_VERSION_STRING " update mode\r\n");
    boot_transport_write_str("[BOOT] type 'help' for commands\r\n");

    for (;;)
    {
        if (boot_update_is_busy(&s_update))
        {
            /* 会话进行中由状态机独占串口：两个读者同时取字节会互相抢。 */
            boot_update_poll(&s_update);
            boot_main_poll_session();
        }
        else
        {
            while (boot_transport_read(&byte))
            {
                ota_cli_feed(&s_cli, &byte, 1u);
            }
        }
    }
}

/**
 * @brief  跳进选中的槽
 * @param  status 决策快照
 * @note   试运行计数在这里才提交：此前任何一步失败都不会白白消耗一次机会。
 */
static void boot_main_launch(boot_status_t *status)
{
    boot_jump_vector_t vector;
    uint32_t flags = 0u;

    if (!boot_jump_vector(status->running_slot, s_chosen.image_size, &vector))
    {
        /* 决策与跳转前的复核不一致，保守地退回升级模式。 */
        boot_main_run_update_mode(status);
        return;
    }

    if (status->is_pending_launch)
    {
        (void)boot_metadata_set_attempts(&s_metadata, s_metadata.boot_attempts + 1u);
        (void)boot_metadata_save(&s_metadata);
        status->boot_attempts = s_metadata.boot_attempts;
        flags = BOOT_MAILBOX_FLAG_PENDING;
    }

    boot_mailbox_publish(status->running_slot, flags, status->boot_attempts);

    boot_transport_deinit();
    boot_jump_to(&vector);
}

void boot_main(void)
{
    boot_nvs_report_t report;

    boot_mailbox_reset();

    s_status.is_metadata_valid = boot_metadata_load(&s_metadata);

    if (boot_main_handle_confirm(&s_metadata))
    {
        s_status.is_metadata_valid = true;
    }

    /* NVS 请求要先于决策处理：它可能刚把 pending 定下来，决策得看得见。 */
    report = boot_nvs_process(BOOT_SLOT_ID_NONE, &s_metadata);
    if (report.result == BOOT_NVS_RESULT_APPLIED)
    {
        s_status.is_metadata_valid = true;
    }

    boot_main_decide(&s_metadata, &s_status);

    s_cli_context.protected_slot = boot_main_protected_slot(&s_metadata, s_status.running_slot);
    s_cli_context.target_slot = boot_main_choose_target(&s_metadata, s_cli_context.protected_slot);

    /* 没得跑，或者 App 明确要求回 Boot，都进升级模式。 */
    if ((s_status.decision == BOOT_DECISION_WAIT_UPDATE) || boot_mailbox_has_request())
    {
        boot_mailbox_consume_request();
        boot_main_run_update_mode(&s_status);
    }

    boot_main_launch(&s_status);

    /* boot_main_launch() 正常不会返回；真回来了就停在升级模式里等人工处理。 */
    boot_main_run_update_mode(&s_status);
}
