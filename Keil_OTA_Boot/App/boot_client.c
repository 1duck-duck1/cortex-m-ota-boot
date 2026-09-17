/**
 ******************************************************************************
 * @file    boot_client.c
 * @brief   App 侧对 Boot 约定的实现
 ******************************************************************************
 */
#include "boot_client.h"
#include "boot_conf.h"
#include "boot_flag.h"
#include "stm32f4xx_hal.h"

void boot_client_init(void)
{
    /* BKP 使能序列与 Boot 侧完全一致（boot_flag.c） */
    boot_flag_init();
}

void boot_client_confirm(void)
{
    BOOT_BKP_CONFIRM = BOOT_MAGIC_CONFIRM;
}

void boot_client_request_update(void)
{
    /* 备份寄存器写入立即生效（APB1 寄存器写，无缓冲），写完即可复位 */
    BOOT_BKP_REQUEST = BOOT_MAGIC_REQUEST;
    NVIC_SystemReset();
}

void boot_client_heartbeat(void)
{
    BOOT_BKP_HEARTBEAT++;
}
