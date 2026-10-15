/**
 ******************************************************************************
 * @file    app_slot_writer.c
 * @brief   非活动槽的流式写会话的实现
 ******************************************************************************
 */
#include "app_slot_writer.h"

#include "app_flash.h"
#include "ota_crc32.h"
#include "ota_image.h"

#include <stddef.h>

/** 回读校验的分块大小：占用栈空间小，384 KB 全量回读的分块开销可以忽略 */
#define APP_SLOT_WRITER_VERIFY_CHUNK    128u

bool app_slot_writer_begin(app_slot_writer_t *writer,
                           uint32_t slot_id,
                           uint32_t running_slot,
                           uint32_t image_size)
{
    uint32_t capacity;

    if (writer == NULL) {
        return false;
    }

    /* 上一次会话若没走完收尾（拒绝 / 中止），在这里丢弃，避免状态串场。 */
    app_slot_writer_end(writer);

    capacity = app_flash_slot_size(slot_id);
    if (capacity == 0u) {
        return false;   /* 不是 Slot A / Slot B */
    }
    if (slot_id == running_slot) {
        return false;   /* 擦运行槽等于立刻砖化 */
    }
    /* 槽首 512 字节留给镜像头，载荷可用空间要从这里扣掉。 */
    capacity -= OTA_IMAGE_LOAD_OFFSET;
    if ((image_size == 0u) || (image_size > capacity)) {
        return false;
    }

    if (!app_flash_erase_slot(slot_id)) {
        return false;
    }

    writer->is_active = true;
    writer->payload_base = app_flash_slot_base(slot_id) + OTA_IMAGE_LOAD_OFFSET;
    writer->payload_capacity = capacity;
    writer->image_size = image_size;
    writer->written = 0u;

    return true;
}

bool app_slot_writer_write(app_slot_writer_t *writer,
                           uint32_t offset,
                           const uint8_t *data,
                           uint32_t size)
{
    if ((writer == NULL) || !writer->is_active || (data == NULL) || (size == 0u)) {
        return false;
    }
    if ((offset != writer->written) || (offset >= writer->image_size)) {
        return false;
    }
    if (size > (writer->image_size - offset)) {
        return false;
    }
    if ((size > writer->payload_capacity) ||
        (offset > (writer->payload_capacity - size))) {
        return false;
    }
    if (!app_flash_range_valid(writer->payload_base + offset, size)) {
        /* 几何硬校验：整段必须落在本槽的载荷区内。这一条把 Boot / Metadata /
           NVS / 保留区全部挡在门外，因为它们的地址区间与两个槽不重叠。 */
        return false;
    }

    if (!app_flash_write(writer->payload_base + offset, data, size)) {
        return false;
    }

    writer->written += size;

    return true;
}

bool app_slot_writer_verify(const app_slot_writer_t *writer, uint32_t *crc32_out)
{
    uint8_t buffer[APP_SLOT_WRITER_VERIFY_CHUNK];
    uint32_t offset = 0u;
    uint32_t chunk;
    uint32_t crc = OTA_CRC32_INIT_VALUE;

    if ((writer == NULL) || !writer->is_active || (crc32_out == NULL)) {
        return false;
    }
    if (writer->written != writer->image_size) {
        /* 没收满就比对等于拿半截数据算 CRC，结果没有意义。 */
        return false;
    }

    while (offset < writer->image_size) {
        chunk = writer->image_size - offset;
        if (chunk > (uint32_t)sizeof(buffer)) {
            chunk = (uint32_t)sizeof(buffer);
        }
        if (!app_flash_read(writer->payload_base + offset, buffer, chunk)) {
            return false;
        }
        crc = ota_crc32_update(crc, buffer, chunk);
        offset += chunk;
    }

    *crc32_out = crc ^ OTA_CRC32_XOROUT_VALUE;

    return true;
}

void app_slot_writer_end(app_slot_writer_t *writer)
{
    if (writer == NULL) {
        return;
    }

    writer->is_active = false;
    writer->payload_base = 0u;
    writer->payload_capacity = 0u;
    writer->image_size = 0u;
    writer->written = 0u;
}
