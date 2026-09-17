/**
 ******************************************************************************
 * @file    boot_flag.c
 * @brief   RTC 备份寄存器标志的读写实现
 ******************************************************************************
 */
#include "boot_flag.h"
#include "boot_conf.h"
#include "stm32f4xx_hal.h"

void boot_flag_init(void)
{
    /* 访问 BKPxR 的三步使能序列，顺序不能换：
     * 1) PWR 外设时钟；
     * 2) PWR->CR.DBP = 1 解锁备份域写；
     * 3) RTC 外电路时钟（BKPxR 挂在 RTC 模块下，无 RTCEN 则访问无效）。
     * 备份寄存器不受 RTC->WPR 影响，无需写保护解锁。 */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_ENABLE();
}

bool boot_flag_request_pending(void)
{
    return BOOT_BKP_REQUEST == BOOT_MAGIC_REQUEST;
}

void boot_flag_request_clear(void)
{
    BOOT_BKP_REQUEST = 0u;
}

void boot_flag_attempts_increment(void)
{
    BOOT_BKP_ATTEMPTS++;
}

uint32_t boot_flag_attempts_get(void)
{
    return BOOT_BKP_ATTEMPTS;
}

void boot_flag_attempts_clear(void)
{
    BOOT_BKP_ATTEMPTS = 0u;
}

bool boot_flag_confirm_pending(void)
{
    return BOOT_BKP_CONFIRM == BOOT_MAGIC_CONFIRM;
}

void boot_flag_confirm_clear(void)
{
    BOOT_BKP_CONFIRM = 0u;
}
