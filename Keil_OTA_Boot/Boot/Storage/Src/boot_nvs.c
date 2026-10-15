/**
 ******************************************************************************
 * @file    boot_nvs.c
 * @brief   参数数据区升级请求的处理实现
 ******************************************************************************
 */
#include "boot_nvs.h"

#include "boot_conf.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_verify.h"
#include "ota_image.h"

#include <string.h>

/**
 * @brief  判断目标槽是否属于"不该被覆盖"的一侧
 * @param  running_slot 本次启动选中的槽
 * @param  metadata     当前 Metadata
 * @param  target_slot  请求声明的目标槽
 * @return true 表示目标槽应当被拒绝
 * @note   正在运行的槽绝不能覆盖。停在升级模式（没有选中任何槽）时，只要
 *         active 槽里确实还有一份能启动的镜像，也同样保护它——否则会把设备
 *         唯一能跑的东西擦掉，再也回不去。
 */
static bool boot_nvs_is_protected(uint32_t running_slot,
                                  const boot_metadata_t *metadata,
                                  uint32_t target_slot)
{
    if (running_slot != BOOT_SLOT_ID_NONE) {
        return (target_slot == running_slot);
    }

    if (metadata->active_slot == BOOT_SLOT_ID_NONE) {
        return false;
    }

    return (target_slot == metadata->active_slot) &&
           (boot_image_check(metadata->active_slot, NULL) == BOOT_IMAGE_VALID);
}

/**
 * @brief  确认目标槽的镜像头已经与本次请求一致
 * @param  slot_id        目标槽
 * @param  version        请求声明的版本号
 * @param  image_size     请求声明的载荷长度
 * @param  payload_crc32  重算出的载荷 CRC32
 * @param  sig_algorithm  请求声明的签名算法标识
 * @param  pubkey_id      请求声明的公钥编号
 * @param  signature      请求携带的签名，允许为 NULL 表示未签名
 * @return true 表示头已存在且内容一致，无需再写
 * @note   用于"上一次处理已补过头但请求没来得及清掉"的重入场景：此时头区
 *         已不是擦除态，直接补写会被拒，但镜像其实是好的。签名字段必须一并
 *         比对：同内容不同签名（例如换了一把公钥）不算"已补过头"。
 */
static bool boot_nvs_header_matches(uint32_t slot_id,
                                    uint32_t version,
                                    uint32_t image_size,
                                    uint32_t payload_crc32,
                                    uint8_t sig_algorithm,
                                    uint8_t pubkey_id,
                                    const uint8_t *signature)
{
    ota_image_header_t header;
    uint32_t base = boot_flash_slot_base(slot_id);

    if (base == 0u) {
        return false;
    }

    if (!boot_flash_read(base, (uint8_t *)&header, (uint32_t)sizeof(header))) {
        return false;
    }
    if (!ota_image_header_verify(&header, BOOT_SLOT_SIZE)) {
        return false;
    }

    if ((header.version != version) ||
        (header.image_size != image_size) ||
        (header.payload_crc32 != payload_crc32)) {
        return false;
    }

    if ((header.sig_algorithm != sig_algorithm) || (header.pubkey_id != pubkey_id)) {
        return false;
    }
    if (signature != NULL) {
        if (memcmp(header.signature, signature, OTA_IMAGE_SIGNATURE_SIZE) != 0) {
            return false;
        }
    }

    return true;
}

bool boot_nvs_read(ota_request_t *request)
{
    if (request == NULL) {
        return false;
    }

    (void)memset(request, 0, sizeof(*request));

    if (!boot_flash_read_nvs(0u, (uint8_t *)request, (uint32_t)sizeof(*request))) {
        return false;
    }

    return ota_request_is_valid(request);
}

void boot_nvs_clear(void)
{
    static const uint8_t cleared[4] = { 0u, 0u, 0u, 0u };

    /* 记录头 4 字节是魔术所在，写成 0 即可让整条记录失效，无需整扇区擦除。 */
    (void)boot_flash_write_nvs(0u, cleared, 4u);
}

boot_nvs_report_t boot_nvs_process(uint32_t running_slot, boot_metadata_t *metadata)
{
    boot_nvs_report_t report;
    ota_request_t request;
    ota_image_header_t verify_header;
    uint32_t base;
    uint32_t crc32 = 0u;

    (void)memset(&report, 0, sizeof(report));
    report.result = BOOT_NVS_RESULT_IDLE;
    report.target_slot = BOOT_SLOT_ID_NONE;

    if (metadata == NULL) {
        report.result = BOOT_NVS_RESULT_INVALID;
        return report;
    }

    /* 空参数区判断走在结构校验之前：从未写过 NVS 的设备不该被报成"请求非法"。 */
    if (boot_flash_is_erased(BOOT_NVS_ADDR, 4u)) {
        return report;
    }

    if (!boot_nvs_read(&request)) {
        report.result = BOOT_NVS_RESULT_INVALID;
        return report;
    }

    report.version = request.version;
    report.target_slot = request.target_slot;
    report.image_size = request.image_size;
    report.image_crc32 = request.image_crc32;

    if ((request.target_slot != BOOT_SLOT_ID_A) && (request.target_slot != BOOT_SLOT_ID_B)) {
        report.result = BOOT_NVS_RESULT_INVALID;
        return report;
    }

    if (boot_nvs_is_protected(running_slot, metadata, request.target_slot)) {
        report.result = BOOT_NVS_RESULT_STALE;
        return report;
    }

    if ((request.image_size == 0u) ||
        (request.image_size > (BOOT_SLOT_SIZE - OTA_IMAGE_LOAD_OFFSET))) {
        report.result = BOOT_NVS_RESULT_RANGE;
        return report;
    }

    /* 复核第一关：重算整槽载荷 CRC32，对上声明才说明槽里真的是那份固件。 */
    if (!boot_image_payload_crc(request.target_slot, request.image_size, &crc32)) {
        report.result = BOOT_NVS_RESULT_CRC_MISMATCH;
        return report;
    }
    if (crc32 != request.image_crc32) {
        report.result = BOOT_NVS_RESULT_CRC_MISMATCH;
        return report;
    }

    /* 复核第二关：向量表首两项必须能启动，挡住"CRC 对但根本跑不起来"的镜像。 */
    base = boot_flash_slot_base(request.target_slot);
    if (!boot_image_vector_valid(base + OTA_IMAGE_LOAD_OFFSET,
                                 base,
                                 request.image_size,
                                 NULL,
                                 NULL)) {
        report.result = BOOT_NVS_RESULT_VECTOR_INVALID;
        return report;
    }

    /* 复核第三关：验证签名。利用请求字段重建与 PC 侧一致的 prologue，再对流式
       扫描槽内载荷求摘要后验签。失败保持请求原样，绝不补头。 */
    (void)memset(&verify_header, 0, sizeof(verify_header));
    verify_header.magic = OTA_IMAGE_MAGIC;
    verify_header.hw_id = OTA_IMAGE_HW_ID;
    verify_header.header_version = OTA_IMAGE_HEADER_VERSION;
    verify_header.header_size = OTA_IMAGE_HEADER_SIZE;
    verify_header.load_offset = OTA_IMAGE_LOAD_OFFSET;
    verify_header.image_size = request.image_size;
    verify_header.payload_crc32 = crc32;
    verify_header.version = request.version;
    if (!boot_verify_image(base, &verify_header, request.signature, request.pubkey_id)) {
        report.result = BOOT_NVS_RESULT_SIGNATURE_INVALID;
        return report;
    }

    /* 复核第四关：补上镜像头。已经补过且内容一致的槽直接沿用。 */
    if (!boot_nvs_header_matches(request.target_slot,
                                 request.version,
                                 request.image_size,
                                 crc32,
                                 request.sig_algorithm,
                                 request.pubkey_id,
                                 request.signature)) {
        if (!boot_image_write_header(request.target_slot,
                                     request.version,
                                     request.image_size,
                                     crc32,
                                     request.sig_algorithm,
                                     request.pubkey_id,
                                     request.signature)) {
            report.result = BOOT_NVS_RESULT_HEADER_FAILED;
            return report;
        }
    }

    if (boot_metadata_mark_pending(metadata, request.target_slot)) {
        if (!boot_metadata_save(metadata)) {
            report.result = BOOT_NVS_RESULT_COMMIT_FAILED;
            return report;
        }
    }

    /* 全部落盘之后才清请求：先清后失败会让固件彻底失去线索。 */
    boot_nvs_clear();

    report.result = BOOT_NVS_RESULT_APPLIED;

    return report;
}

const char *boot_nvs_result_name(boot_nvs_result_t result)
{
    switch (result) {
        case BOOT_NVS_RESULT_IDLE:
            return "IDLE";
        case BOOT_NVS_RESULT_INVALID:
            return "INVALID";
        case BOOT_NVS_RESULT_STALE:
            return "STALE";
        case BOOT_NVS_RESULT_RANGE:
            return "RANGE";
        case BOOT_NVS_RESULT_CRC_MISMATCH:
            return "CRC_MISMATCH";
        case BOOT_NVS_RESULT_VECTOR_INVALID:
            return "VECTOR_INVALID";
        case BOOT_NVS_RESULT_SIGNATURE_INVALID:
            return "SIGNATURE_INVALID";
        case BOOT_NVS_RESULT_HEADER_FAILED:
            return "HEADER_FAILED";
        case BOOT_NVS_RESULT_COMMIT_FAILED:
            return "COMMIT_FAILED";
        case BOOT_NVS_RESULT_APPLIED:
            return "APPLIED";
        default:
            return "UNKNOWN";
    }
}
