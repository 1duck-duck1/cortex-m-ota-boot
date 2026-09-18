/**
 ******************************************************************************
 * @file    boot_main.c
 * @brief   Bootloader 主流程状态机
 ******************************************************************************
 */
#include "boot_main.h"
#include "boot_conf.h"
#include "boot_flag.h"
#include "boot_jump.h"
#include "boot_metadata.h"
#include "boot_flash.h"
#include "stm32f4xx_hal.h"

/* v0.1.0 固定启动 Slot A；后续由持久化元数据选择活动槽。 */
static const boot_slot_t s_slot_a =
{
    BOOT_SLOT_ID_A,
    BOOT_SLOT_A_ADDR,
    BOOT_SLOT_A_SIZE
};

static const boot_slot_t s_slot_b =
{
    BOOT_SLOT_ID_B,
    BOOT_SLOT_B_ADDR,
    BOOT_SLOT_B_SIZE
};

/* 静态保存，便于复位前在 Keil Watch 中持续观察。 */
static boot_context_t s_boot_context;

static void boot_transition(boot_context_t *context, boot_state_t next_state)
{
    context->previous_state = context->state;
    context->state = next_state;
}

/**
 * @brief  升级停留模式的一次处理
 * @note   v0.2.0 在此接入传输层；当前仅等待中断以降低空闲功耗。
 */
static void boot_wait_update(void)
{
    __WFI();
}

boot_status_t boot_context_init(boot_context_t *context)
{
    if (context == NULL)
    {
        return BOOT_STATUS_INVALID_PARAM;
    }

    /* 将上下文重置为初始状态。 */
    context->state = BOOT_STATE_INIT;
    context->previous_state = BOOT_STATE_INIT;
    context->last_status = BOOT_STATUS_OK;
    context->wait_reason = BOOT_WAIT_REASON_NONE;
    context->boot_slot = s_slot_a;
    context->metadata.magic = 0u;
    context->boot_attempts = 0u;
    context->update_requested = false;
    context->app_confirmed = false;

    return BOOT_STATUS_OK;
}

boot_status_t boot_process(boot_context_t *context)
{
    boot_status_t status = BOOT_STATUS_OK;

    if (context == NULL)
    {
        return BOOT_STATUS_INVALID_PARAM;
    }

    switch (context->state)
    {
        case BOOT_STATE_INIT:
            boot_flag_init();
            (void)boot_flash_init();
            boot_transition(context, BOOT_STATE_LOAD_METADATA);
            break;

        case BOOT_STATE_LOAD_METADATA:
            if (boot_metadata_load(&context->metadata) != BOOT_STATUS_OK)
            {
                /* 首次启动或元数据损坏：以 Slot A 为唯一已知固件。 */
                context->metadata.sequence = 0u;
                context->metadata.active_slot = BOOT_SLOT_ID_A;
                context->metadata.confirmed_slot = BOOT_SLOT_ID_A;
                context->metadata.pending_slot = BOOT_SLOT_ID_NONE;
                context->metadata.slot_a.state = BOOT_IMAGE_STATE_CONFIRMED;
                context->metadata.slot_a.image_size = BOOT_SLOT_A_SIZE;
                (void)boot_metadata_commit(&context->metadata);
            }
            boot_transition(context, BOOT_STATE_LOAD_FLAGS);
            break;

        case BOOT_STATE_LOAD_FLAGS:
            context->app_confirmed = boot_flag_confirm_pending();
            context->update_requested = boot_flag_request_pending();
            context->boot_attempts = context->metadata.boot_attempts;
            boot_transition(context, BOOT_STATE_DECIDE);
            break;

        case BOOT_STATE_DECIDE:
            /* App 确认优先消费：确认语义是"当前运行的固件健康"。
             * 存在 pending 切换时，完成切换并把 pending 槽转正；
             * 无 pending 时把 active 槽标为 CONFIRMED 并清零计数——
             * 清零必须提交落盘，否则下次 LOAD_FLAGS 又从元数据读回
             * 旧值继续累积，最终误触发回滚。元数据本来就干净
             * （pending==NONE 且计数==0）时跳过提交，避免多余擦写。 */
            if (context->app_confirmed)
            {
                if ((context->metadata.pending_slot != BOOT_SLOT_ID_NONE) ||
                    (context->metadata.boot_attempts != 0u))
                {
                    boot_slot_id_t slot_to_confirm =
                        (context->metadata.pending_slot != BOOT_SLOT_ID_NONE) ?
                        (boot_slot_id_t)context->metadata.pending_slot :
                        (boot_slot_id_t)context->metadata.active_slot;

                    status = boot_metadata_confirm(&context->metadata,
                                                   slot_to_confirm);
                    if (status != BOOT_STATUS_OK)
                    {
                        context->wait_reason = BOOT_WAIT_REASON_INTERNAL_ERROR;
                        boot_transition(context, BOOT_STATE_ERROR);
                        break;
                    }
                }
                boot_flag_confirm_clear();
                context->app_confirmed = false;
                context->boot_attempts = 0u;
            }

            /* App 主动请求升级：消费一次性请求并停留 Boot。 */
            if (context->update_requested)
            {
                boot_flag_request_clear();
                context->update_requested = false;
                context->wait_reason = BOOT_WAIT_REASON_UPDATE_REQUESTED;
                boot_transition(context, BOOT_STATE_WAIT_UPDATE);
                break;
            }

            /* 连续启动达到阈值时拒绝继续跳转。 */
            if (context->boot_attempts >= BOOT_MAX_ATTEMPTS)
            {
                if (context->metadata.pending_slot != BOOT_SLOT_ID_NONE)
                {
                    status = boot_metadata_rollback(&context->metadata);
                    if (status != BOOT_STATUS_OK)
                    {
                        context->wait_reason = BOOT_WAIT_REASON_INTERNAL_ERROR;
                        boot_transition(context, BOOT_STATE_ERROR);
                        break;
                    }
                }
                context->wait_reason = BOOT_WAIT_REASON_ATTEMPTS_EXCEEDED;
                boot_transition(context, BOOT_STATE_WAIT_UPDATE);
                break;
            }

            if (context->metadata.pending_slot != BOOT_SLOT_ID_NONE)
            {
                context->boot_slot = (context->metadata.pending_slot == BOOT_SLOT_ID_A) ?
                                     s_slot_a :
                                     s_slot_b;
            }
            else
            {
                context->boot_slot = (context->metadata.active_slot == BOOT_SLOT_ID_A) ?
                                     s_slot_a :
                                     s_slot_b;
            }
            boot_transition(context, BOOT_STATE_PREPARE_BOOT);
            break;

        case BOOT_STATE_PREPARE_BOOT:
            /* 必须在跳转前提交计数，保证 App 崩溃后能够累计。 */
            context->metadata.boot_attempts++;
            context->metadata.sequence++;
            status = boot_metadata_commit(&context->metadata);
            if (status != BOOT_STATUS_OK)
            {
                context->wait_reason = BOOT_WAIT_REASON_INTERNAL_ERROR;
                boot_transition(context, BOOT_STATE_ERROR);
                break;
            }
            context->boot_attempts++;
            boot_transition(context, BOOT_STATE_BOOT_APP);
            break;

        case BOOT_STATE_BOOT_APP:
            if (!boot_jump_slot(context->boot_slot.address,
                                context->boot_slot.size))
            {
                context->wait_reason = BOOT_WAIT_REASON_INVALID_IMAGE;
                status = BOOT_STATUS_INVALID_IMAGE;
                boot_transition(context, BOOT_STATE_WAIT_UPDATE);
            }
            else
            {
                /* 跳转成功不会返回；返回 true 表示控制流出现异常。 */
                context->wait_reason = BOOT_WAIT_REASON_INTERNAL_ERROR;
                status = BOOT_STATUS_JUMP_RETURNED;
                boot_transition(context, BOOT_STATE_ERROR);
            }
            break;

        case BOOT_STATE_WAIT_UPDATE:
            boot_wait_update();
            break;

        case BOOT_STATE_ERROR:
            boot_wait_update();
            break;

        default:
            context->wait_reason = BOOT_WAIT_REASON_INTERNAL_ERROR;
            status = BOOT_STATUS_INVALID_STATE;
            boot_transition(context, BOOT_STATE_ERROR);
            break;
    }

    /* 保留最近一次错误，避免 WAIT_UPDATE 下一轮把诊断信息覆盖为 OK。 */
    if (status != BOOT_STATUS_OK)
    {
        context->last_status = status;
    }
    return status;
}

const boot_context_t *boot_get_context(void)
{
    return &s_boot_context;
}

void boot_run(void)
{
    boot_status_t status;

    status = boot_context_init(&s_boot_context);
    if (status != BOOT_STATUS_OK)
    {
        while (1)
        {
            __WFI();
        }
    }

    while (1)
    {
        (void)boot_process(&s_boot_context);
    }
}
