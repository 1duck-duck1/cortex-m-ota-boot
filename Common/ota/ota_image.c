/**
 ******************************************************************************
 * @file    ota_image.c
 * @brief   槽内镜像头的填充与校验实现
 ******************************************************************************
 */
#include "ota_image.h"

#include "ota_crc32.h"

#include <stddef.h>

uint32_t ota_image_header_crc(const ota_image_header_t *header)
{
    const uint8_t *bytes = (const uint8_t *)header;
    uint32_t crc_size = (uint32_t)sizeof(ota_image_header_t);
    uint32_t crc_field = (uint32_t)offsetof(ota_image_header_t, header_crc32);
    uint32_t crc = OTA_CRC32_INIT_VALUE;

    /* 分两段跳过 header_crc32 字段：它按 0 参与，等价于"先清零再整体计算"，
       省掉一份 512 字节的临时缓冲区。 */
    crc = ota_crc32_update(crc, bytes, crc_field);
    crc = ota_crc32_update(crc, bytes + crc_field + (uint32_t)sizeof(uint32_t),
                           crc_size - crc_field - (uint32_t)sizeof(uint32_t));

    return ota_crc32_final(crc);
}

void ota_image_header_fill(ota_image_header_t *header,
                           uint16_t hw_id,
                           uint32_t version,
                           uint32_t image_size,
                           uint32_t payload_crc32,
                           uint8_t sig_algorithm,
                           uint8_t pubkey_id,
                           const uint8_t *signature)
{
    uint32_t index;

    header->magic = OTA_IMAGE_MAGIC;
    header->hw_id = hw_id;
    header->header_version = OTA_IMAGE_HEADER_VERSION;
    header->header_size = OTA_IMAGE_HEADER_SIZE;
    header->load_offset = OTA_IMAGE_LOAD_OFFSET;
    header->image_size = image_size;
    header->payload_crc32 = payload_crc32;
    header->version = version;
    header->header_crc32 = 0u;

    /* 未签名是"清零 + 置 NONE"，避免调用方漏填时把 0xFF 残留当成有效签名。 */
    if (signature == NULL) {
        header->sig_algorithm = OTA_IMAGE_SIG_NONE;
        header->pubkey_id = 0u;
        for (index = 0u; index < OTA_IMAGE_SIGNATURE_SIZE; index++) {
            header->signature[index] = 0u;
        }
    } else {
        header->sig_algorithm = sig_algorithm;
        header->pubkey_id = pubkey_id;
        for (index = 0u; index < OTA_IMAGE_SIGNATURE_SIZE; index++) {
            header->signature[index] = signature[index];
        }
    }

    header->reserved0[0] = 0u;
    header->reserved0[1] = 0u;
    header->reserved1[0] = 0u;
    header->reserved1[1] = 0u;

    /* 头 CRC32 必须最后算、最后写：掉电落在这一步之前，头就是无效的。 */
    header->header_crc32 = ota_image_header_crc(header);
}

bool ota_image_header_verify(const ota_image_header_t *header, uint32_t slot_size)
{
    if (header->magic != OTA_IMAGE_MAGIC) {
        return false;
    }
    if (header->hw_id != OTA_IMAGE_HW_ID) {
        return false;
    }
    if (header->header_version != OTA_IMAGE_HEADER_VERSION) {
        return false;
    }
    if (header->header_size != OTA_IMAGE_HEADER_SIZE) {
        return false;
    }
    if (header->load_offset != OTA_IMAGE_LOAD_OFFSET) {
        return false;
    }
    if (slot_size <= OTA_IMAGE_LOAD_OFFSET) {
        return false;
    }
    if ((header->image_size == 0u) ||
        (header->image_size > (slot_size - OTA_IMAGE_LOAD_OFFSET))) {
        return false;
    }
    if (header->header_crc32 != ota_image_header_crc(header)) {
        return false;
    }

    return true;
}
