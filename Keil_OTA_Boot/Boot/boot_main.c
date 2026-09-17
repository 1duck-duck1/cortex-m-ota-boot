/**
 ******************************************************************************
 * @file    boot_main.c
 * @brief   Bootloader 主流程：确认消费 → 回跳消费 → 计数保护 → 校验跳转
 ******************************************************************************
 */
#include "boot_main.h"
#include "boot_conf.h"
#include "boot_flag.h"
#include "boot_jump.h"
#include "stm32f4xx_hal.h"

/**
 * @brief  升级停留模式：等待宿主机命令
 * @note   v0.2.0 在此接入固件接收（YMODEM 等），v0.1.0 仅停留
 */
static void boot_wait_update(void)
{
    while (1)
    {
        __WFI();
    }
}

void boot_run(void)
{
    boot_flag_init();

    /* 消费 App 确认：新固件已自检通过，清启动计数（F-15 雏形） */
    if (boot_flag_confirm_pending())
    {
        boot_flag_confirm_clear();
        boot_flag_attempts_clear();
    }

    /* 消费回跳请求：App 主动要求升级，进入升级停留（F-03） */
    if (boot_flag_request_pending())
    {
        boot_flag_request_clear();
        boot_wait_update();  /* 不返回 */
    }

    /* 连续启动超限：App 反复崩溃未确认，拒绝继续跳转（F-04） */
    if (boot_flag_attempts_get() >= BOOT_MAX_ATTEMPTS)
    {
        boot_wait_update();  /* 不返回 */
    }

    /* 尝试启动 Slot A。
     * v0.1.0 单槽写死（铁律 R-1：先跑通再抽象）；A/B 切换属 v0.3.0。
     * 计数必须先于跳转提交（架构文档 §7.4：否则 App 崩溃后计数不累积） */
    boot_flag_attempts_increment();
    if (!boot_jump_slot(BOOT_SLOT_A_ADDR, BOOT_SLOT_A_SIZE))
    {
        boot_wait_update();  /* 校验失败（空槽等），不返回 */
    }

    /* boot_jump_slot 成功时不返回，走到这里说明跳转异常 */
    while (1)
    {
    }
}
