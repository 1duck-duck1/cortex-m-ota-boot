/**
 ******************************************************************************
 * @file    boot_slot_writer.c
 * @brief   目标槽写入会话的实现
 ******************************************************************************
 */
#include "boot_slot_writer.h"

#include <string.h>

#include "boot_conf.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "ota_crc32.h"
#include "ota_image.h"

/**
 * @brief  校验一次写入请求的几何合法性
 * @param  writer 会话对象
 * @param  size   本次待写入的字节数
 * @return true 表示可以写
 */
static bool boot_slot_writer_accepts(const boot_slot_writer_t *writer, uint32_t size)
{
    if (!writer->is_open || (size == 0u))
    {
        return false;
    }
    if (writer->written >= writer->expected_size)
    {
        return false;
    }

    return true;
}

bool boot_slot_writer_begin(boot_slot_writer_t *writer,
                            uint32_t slot_id,
                            uint32_t protected_slot,
                            uint32_t expected_size)
{
    uint32_t base;

    if (writer == NULL)
    {
        return false;
    }

    (void)memset(writer, 0, sizeof(*writer));

    if ((slot_id != BOOT_SLOT_ID_A) && (slot_id != BOOT_SLOT_ID_B))
    {
        return false;
    }

    /* 擦掉正在跑的槽等于把自己脚下的地板抽掉，必须挡住。 */
    if ((protected_slot != BOOT_SLOT_ID_NONE) && (slot_id == protected_slot))
    {
        return false;
    }

    if ((expected_size == 0u) || (expected_size > (BOOT_SLOT_SIZE - OTA_IMAGE_LOAD_OFFSET)))
    {
        return false;
    }

    base = boot_flash_slot_base(slot_id);
    if (base == 0u)
    {
        return false;
    }

    if (!boot_flash_erase_slot(slot_id))
    {
        return false;
    }

    writer->slot_id = slot_id;
    writer->base = base;
    writer->expected_size = expected_size;
    writer->written = 0u;
    writer->crc = OTA_CRC32_INIT_VALUE;
    writer->is_open = true;

    return true;
}

bool boot_slot_writer_write(boot_slot_writer_t *writer, const uint8_t *data, uint32_t size)
{
    uint32_t remaining;
    uint32_t address;

    if ((writer == NULL) || (data == NULL) || !boot_slot_writer_accepts(writer, size))
    {
        return false;
    }

    remaining = writer->expected_size - writer->written;
    if (size > remaining)
    {
        size = remaining;
    }

    address = writer->base + OTA_IMAGE_LOAD_OFFSET + writer->written;

    if (!boot_flash_write(address, data, size))
    {
        return false;
    }

    writer->crc = ota_crc32_update(writer->crc, data, size);
    writer->written += size;

    return true;
}

bool boot_slot_writer_verify(const boot_slot_writer_t *writer, uint32_t *crc32_out)
{
    uint32_t expected;
    uint32_t actual = 0u;

    if ((writer == NULL) || (writer->written == 0u))
    {
        return false;
    }

    expected = ota_crc32_final(writer->crc);
    if (crc32_out != NULL)
    {
        *crc32_out = expected;
    }

    if (!boot_image_payload_crc(writer->slot_id, writer->written, &actual))
    {
        return false;
    }

    return (actual == expected);
}

bool boot_slot_writer_finish(boot_slot_writer_t *writer,
                             uint32_t *image_size_out,
                             uint32_t *crc32_out)
{
    if (writer == NULL)
    {
        return false;
    }
    if (!writer->is_open || (writer->written != writer->expected_size))
    {
        return false;
    }

    if (image_size_out != NULL)
    {
        *image_size_out = writer->written;
    }
    if (crc32_out != NULL)
    {
        *crc32_out = ota_crc32_final(writer->crc);
    }

    writer->is_open = false;

    return true;
}

void boot_slot_writer_abort(boot_slot_writer_t *writer)
{
    if (writer == NULL)
    {
        return;
    }

    writer->is_open = false;
}

bool boot_slot_writer_is_open(const boot_slot_writer_t *writer)
{
    return (writer != NULL) && writer->is_open;
}
