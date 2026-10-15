/**
 ******************************************************************************
 * @file    boot_adopt.c
 * @brief   补镜像头并提交 pending 的实现
 ******************************************************************************
 */
#include "boot_adopt.h"

#include "boot_conf.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_verify.h"
#include "ota_image.h"
#include "ota_request.h"

#include <string.h>

boot_adopt_result_t boot_adopt_slot(uint32_t slot_id,
                                    uint32_t protected_slot,
                                    uint32_t version,
                                    boot_metadata_t *metadata,
                                    boot_adopt_report_t *report)
{
    boot_adopt_report_t local;
    uint32_t base;
    uint32_t image_size = 0u;
    uint32_t crc32 = 0u;

    (void)memset(&local, 0, sizeof(local));
    local.result = BOOT_ADOPT_ERR_SLOT;
    local.slot_id = slot_id;

    if ((slot_id != BOOT_SLOT_ID_A) && (slot_id != BOOT_SLOT_ID_B)) {
        goto finish;
    }

    /* 量产模式要求签名，而 adopt 通道没有私钥、无法生成签名，只能直接拒绝。 */
    local.result = BOOT_ADOPT_ERR_SIGNATURE;
    if (boot_verify_required()) {
        goto finish;
    }

    local.result = BOOT_ADOPT_ERR_PROTECTED;
    if ((protected_slot != BOOT_SLOT_ID_NONE) && (slot_id == protected_slot)) {
        goto finish;
    }

    /* 版本号是防回滚的依据，缺了就没法判断新旧，只能拒绝。 */
    local.result = BOOT_ADOPT_ERR_VERSION;
    if ((metadata == NULL) || (version == OTA_REQUEST_VERSION_NONE)) {
        goto finish;
    }

    base = boot_flash_slot_base(slot_id);
    if (base == 0u) {
        local.result = BOOT_ADOPT_ERR_SLOT;
        goto finish;
    }

    /* 头区必须是擦除态：新旧两次编程会按位相与，得到一份两个来源都不是的头。 */
    local.result = BOOT_ADOPT_ERR_HEADER;
    if (!boot_flash_is_erased(base, OTA_IMAGE_HEADER_SIZE)) {
        goto finish;
    }

    /* 从槽内容反推边界：向量表给出可启动入口，槽尾给出最后一个有效字节。 */
    local.result = BOOT_ADOPT_ERR_NO_IMAGE;
    if (!boot_image_measure(slot_id, &image_size)) {
        goto finish;
    }
    local.image_size = image_size;

    local.result = BOOT_ADOPT_ERR_FAILED;
    if (!boot_image_payload_crc(slot_id, image_size, &crc32)) {
        goto finish;
    }
    local.image_crc32 = crc32;

    /* 开发模式（BOOT_REQUIRE_SIGNATURE=0）才可达此处：补一份未签名的头。 */
    if (!boot_image_write_header(slot_id,
                                 version,
                                 image_size,
                                 crc32,
                                 OTA_IMAGE_SIG_NONE,
                                 0u,
                                 NULL)) {
        goto finish;
    }

    /* 提交 pending 而不是 active：让这份镜像也走一遍试运行与 App 确认。 */
    if (boot_metadata_mark_pending(metadata, slot_id)) {
        if (!boot_metadata_save(metadata)) {
            goto finish;
        }
    }

    local.result = BOOT_ADOPT_OK;

finish:
    if (report != NULL) {
        *report = local;
    }

    return local.result;
}

const char *boot_adopt_result_name(boot_adopt_result_t result)
{
    switch (result) {
        case BOOT_ADOPT_OK:
            return "OK";
        case BOOT_ADOPT_ERR_SLOT:
            return "BAD_SLOT";
        case BOOT_ADOPT_ERR_PROTECTED:
            return "PROTECTED";
        case BOOT_ADOPT_ERR_VERSION:
            return "BAD_VERSION";
        case BOOT_ADOPT_ERR_SIGNATURE:
            return "SIGNATURE_REQUIRED";
        case BOOT_ADOPT_ERR_HEADER:
            return "HEADER_EXISTS";
        case BOOT_ADOPT_ERR_NO_IMAGE:
            return "NO_IMAGE";
        case BOOT_ADOPT_ERR_FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}
