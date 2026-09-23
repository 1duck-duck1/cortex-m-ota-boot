/**
 ******************************************************************************
 * @file    app_slot_writer.c
 * @brief   非活动槽的流式写会话
 ******************************************************************************
 */
#include "app_slot_writer.h"

#include "app_flash.h"
#include "ota_crc32.h"
#include "ota_image.h"

#include <stddef.h>

/** 回读校验的分块大小：占用栈空间小，384 KB 全量回读的分块开销可以忽略 */
#define APP_SLOT_WRITER_VERIFY_CHUNK    128u

static bool s_is_active;
static uint32_t s_payload_base;
static uint32_t s_payload_capacity;
static uint32_t s_image_size;
static uint32_t s_written;

bool app_slot_writer_begin(uint32_t slot_id,
                           uint32_t running_slot,
                           uint32_t image_size)
{
    uint32_t capacity;

    /* 上一次会话若没走完收尾（拒绝 / 中止），在这里丢弃，避免状态串场。 */
    app_slot_writer_end();

    capacity = app_flash_slot_size(slot_id);
    if (capacity == 0u)
    {
        return false;   /* 不是 Slot A / Slot B */
    }
    if (slot_id == running_slot)
    {
        return false;   /* 擦运行槽等于立刻砖化 */
    }
    /* 槽首 512 字节留给镜像头，载荷可用空间要从这里扣掉。 */
    capacity -= OTA_IMAGE_LOAD_OFFSET;
    if ((image_size == 0u) || (image_size > capacity))
    {
        return false;
    }

    if (!app_flash_erase_slot(slot_id))
    {
        return false;
    }

    s_is_active = true;
    s_payload_base = app_flash_slot_base(slot_id) + OTA_IMAGE_LOAD_OFFSET;
    s_payload_capacity = capacity;
    s_image_size = image_size;
    s_written = 0u;

    return true;
}

bool app_slot_writer_write(uint32_t offset, const uint8_t *data, uint32_t size)
{
    if (!s_is_active || (data == NULL) || (size == 0u))
    {
        return false;
    }
    if ((offset != s_written) || (offset >= s_image_size))
    {
        return false;
    }
    if (size > (s_image_size - offset))
    {
        return false;
    }
    if ((size > s_payload_capacity) || (offset > (s_payload_capacity - size)))
    {
        return false;
    }
    if (!app_flash_range_valid(s_payload_base + offset, size))
    {
        /* 几何硬校验：整段必须落在本槽的载荷区内。这一条把 Boot / Metadata /
           NVS / 保留区全部挡在门外，因为它们的地址区间与两个槽不重叠。 */
        return false;
    }

    if (!app_flash_write(s_payload_base + offset, data, size))
    {
        return false;
    }

    s_written += size;

    return true;
}

bool app_slot_writer_verify(uint32_t *crc32_out)
{
    uint8_t buffer[APP_SLOT_WRITER_VERIFY_CHUNK];
    uint32_t offset = 0u;
    uint32_t chunk;
    uint32_t crc = OTA_CRC32_INIT_VALUE;

    if (!s_is_active || (crc32_out == NULL))
    {
        return false;
    }
    if (s_written != s_image_size)
    {
        /* 没收满就比对等于拿半截数据算 CRC，结果没有意义。 */
        return false;
    }

    while (offset < s_image_size)
    {
        chunk = s_image_size - offset;
        if (chunk > (uint32_t)sizeof(buffer))
        {
            chunk = (uint32_t)sizeof(buffer);
        }
        if (!app_flash_read(s_payload_base + offset, buffer, chunk))
        {
            return false;
        }
        crc = ota_crc32_update(crc, buffer, chunk);
        offset += chunk;
    }

    *crc32_out = crc ^ OTA_CRC32_XOROUT_VALUE;

    return true;
}

void app_slot_writer_end(void)
{
    s_is_active = false;
    s_payload_base = 0u;
    s_payload_capacity = 0u;
    s_image_size = 0u;
    s_written = 0u;
}
