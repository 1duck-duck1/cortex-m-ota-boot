/**
 ******************************************************************************
 * @file    app_boot_client.c
 * @brief   基于 SRAM2 保留邮箱的 Bootloader 通信实现
 ******************************************************************************
 */
#include "app_boot_client.h"

#include "app_config.h"
#include "ota_image.h"

#include "stm32f4xx_hal.h"

typedef struct
{
    uint32_t magic;         /**< APP_MAILBOX_MAGIC，整块内容有效的标志 */
    uint32_t request;       /**< APP_MAGIC_REQUEST 时表示请求回到 Boot */
    uint32_t confirmation;  /**< APP_MAGIC_CONFIRM 时表示确认试运行成功 */
    uint32_t attempts;      /**< Boot 写入：本次启动是第几次试运行 */
    uint32_t heartbeat;     /**< 本侧写入：仅供调试观察的存活计数 */
    uint32_t launched_slot; /**< Boot 写入：本次实际启动的槽 */
    uint32_t launch_flags;  /**< Boot 写入：APP_MAILBOX_FLAG_PENDING_LAUNCH 表示试运行启动 */
    uint32_t crc32;         /**< 整块 CRC32，覆盖本字段之前的 28 字节 */
} app_mailbox_t;

static volatile app_mailbox_t *const s_mailbox =
    (volatile app_mailbox_t *)APP_MAILBOX_ADDR;

static uint32_t app_mailbox_crc(void)
{
    const volatile uint32_t *data = &s_mailbox->magic;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t index;
    uint32_t bit;

    for (index = 0u; index < 7u; index++) {
        crc ^= data[index];
        for (bit = 0u; bit < 32u; bit++) {
            crc = ((crc & 1u) != 0u) ?
                  ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void app_mailbox_commit(void)
{
    s_mailbox->magic = APP_MAILBOX_MAGIC;
    s_mailbox->crc32 = app_mailbox_crc();
    __DSB();
}

static bool app_mailbox_valid(void)
{
    return (s_mailbox->magic == APP_MAILBOX_MAGIC) &&
           (s_mailbox->crc32 == app_mailbox_crc());
}

static void app_mailbox_prepare(void)
{
    if (!app_mailbox_valid()) {
        s_mailbox->request = 0u;
        s_mailbox->confirmation = 0u;
        s_mailbox->attempts = 0u;
        s_mailbox->heartbeat = 0u;
        s_mailbox->launched_slot = APP_SLOT_ID_NONE;
        s_mailbox->launch_flags = 0u;
        app_mailbox_commit();
    }
}

void app_boot_client_init(void)
{
    /* 向量表位于槽首 + 镜像头偏移处，与 Boot 跳转时的 SCB->VTOR 取值一致。 */
    SCB->VTOR = APP_SLOT_BASE + OTA_IMAGE_LOAD_OFFSET;
    __DSB();
    __ISB();
}

void app_boot_client_confirm(void)
{
    app_mailbox_prepare();
    s_mailbox->confirmation = APP_MAGIC_CONFIRM;
    app_mailbox_commit();
}

bool app_boot_client_is_pending(void)
{
    return app_mailbox_valid() &&
           ((s_mailbox->launch_flags & APP_MAILBOX_FLAG_PENDING_LAUNCH) != 0u) &&
           (s_mailbox->launched_slot == APP_SLOT_ID);
}

void app_boot_client_confirm_and_reset(void)
{
    __disable_irq();
    app_boot_client_confirm();
    NVIC_SystemReset();

    /* NVIC_SystemReset() 正常不会返回。 */
    while (1) {
        __WFI();
    }
}

void app_boot_client_request_boot(void)
{
    __disable_irq();
    app_mailbox_prepare();
    s_mailbox->request = APP_MAGIC_REQUEST;
    app_mailbox_commit();
    NVIC_SystemReset();

    /* NVIC_SystemReset() 正常不会返回。 */
    while (1) {
        __WFI();
    }
}

void app_boot_client_heartbeat(void)
{
    app_mailbox_prepare();
    s_mailbox->heartbeat++;
    app_mailbox_commit();
}
