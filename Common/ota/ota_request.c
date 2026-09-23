/**
 ******************************************************************************
 * @file    ota_request.c
 * @brief   升级请求记录的填充与结构校验
 ******************************************************************************
 */
#include "ota_request.h"

#include "ota_crc32.h"

#include <stddef.h>

/**
 * @brief  计算记录 CRC32 覆盖的字节数
 * @return record_crc32 之前的字节数，即 20 字节
 */
static uint32_t ota_request_body_size(void)
{
    return (uint32_t)offsetof(ota_request_t, record_crc32);
}

void ota_request_build(ota_request_t *request,
                       uint32_t version,
                       uint32_t target_slot,
                       uint32_t image_size,
                       uint32_t image_crc32)
{
    request->magic = OTA_REQUEST_MAGIC;
    request->version = version;
    request->target_slot = target_slot;
    request->image_size = image_size;
    request->image_crc32 = image_crc32;
    request->record_crc32 = ota_crc32_calc((const uint8_t *)request,
                                           ota_request_body_size());
}

bool ota_request_is_valid(const ota_request_t *request)
{
    uint32_t crc;

    if (request->magic != OTA_REQUEST_MAGIC)
    {
        return false;
    }
    if (request->version == OTA_REQUEST_VERSION_NONE)
    {
        return false;
    }
    if (request->image_size == 0u)
    {
        return false;
    }

    crc = ota_crc32_calc((const uint8_t *)request, ota_request_body_size());

    return (crc == request->record_crc32);
}
