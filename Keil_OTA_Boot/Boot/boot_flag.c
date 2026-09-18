/**
 ******************************************************************************
 * @file    boot_flag.c
 * @brief   .noinit SRAM 邮箱的跨软件复位标志实现
 ******************************************************************************
 */
#include "boot_flag.h"
#include "boot_conf.h"

typedef struct
{
    uint32_t magic;
    uint32_t request;
    uint32_t confirmation;
    uint32_t attempts;
    uint32_t heartbeat;
    uint32_t reserved[2];
    uint32_t crc32;
} boot_mailbox_t;

static volatile boot_mailbox_t *const s_mailbox =
    (volatile boot_mailbox_t *)BOOT_MAILBOX_ADDR;

static uint32_t boot_mailbox_crc(void)
{
    const volatile uint32_t *data = &s_mailbox->magic;
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0u; i < 7u; i++)
    {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 32u; bit++)
        {
            crc = ((crc & 1u) != 0u) ?
                  ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

static bool boot_mailbox_valid(void)
{
    return (s_mailbox->magic == BOOT_MAILBOX_MAGIC) &&
           (s_mailbox->crc32 == boot_mailbox_crc());
}

static void boot_mailbox_commit(void)
{
    s_mailbox->magic = BOOT_MAILBOX_MAGIC;
    s_mailbox->crc32 = boot_mailbox_crc();
}

void boot_flag_init(void)
{
    if (!boot_mailbox_valid())
    {
        s_mailbox->request = 0u;
        s_mailbox->confirmation = 0u;
        s_mailbox->attempts = 0u;
        s_mailbox->heartbeat = 0u;
        s_mailbox->reserved[0] = 0u;
        s_mailbox->reserved[1] = 0u;
        boot_mailbox_commit();
    }
}

bool boot_flag_request_pending(void)
{
    return s_mailbox->request == BOOT_MAGIC_REQUEST;
}

void boot_flag_request_clear(void)
{
    s_mailbox->request = 0u;
    boot_mailbox_commit();
}

void boot_flag_attempts_increment(void)
{
    if (s_mailbox->attempts < UINT32_MAX)
    {
        s_mailbox->attempts++;
    }
    boot_mailbox_commit();
}

uint32_t boot_flag_attempts_get(void)
{
    return s_mailbox->attempts;
}

void boot_flag_attempts_clear(void)
{
    s_mailbox->attempts = 0u;
    boot_mailbox_commit();
}

bool boot_flag_confirm_pending(void)
{
    return s_mailbox->confirmation == BOOT_MAGIC_CONFIRM;
}

void boot_flag_confirm_clear(void)
{
    s_mailbox->confirmation = 0u;
    boot_mailbox_commit();
}

void boot_flag_request_set(void)
{
    s_mailbox->request = BOOT_MAGIC_REQUEST;
    boot_mailbox_commit();
}

void boot_flag_confirm_set(void)
{
    s_mailbox->confirmation = BOOT_MAGIC_CONFIRM;
    boot_mailbox_commit();
}

void boot_flag_heartbeat_increment(void)
{
    s_mailbox->heartbeat++;
    boot_mailbox_commit();
}
