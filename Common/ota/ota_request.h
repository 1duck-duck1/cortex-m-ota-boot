/**
 ******************************************************************************
 * @file    ota_request.h
 * @brief   参数数据区（NVS）里的升级请求记录
 *
 * App 收完包后不能写 Metadata（那是 Boot 的职责），只能把一份"声明"落到 NVS：
 * 目标槽、长度、CRC32 与版本。Boot 下次启动读到它，必须对目标槽重算整槽 CRC32
 * 并与声明比对，通过后才提交 Pending——声明本身不构成任何信任。
 ******************************************************************************
 */
#ifndef OTA_REQUEST_H
#define OTA_REQUEST_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_image.h"

#define OTA_REQUEST_MAGIC          (0x4F544152u)   /* ASCII "OTAR"，记录完整性的第一道门 */
#define OTA_REQUEST_VERSION_NONE   (0u)            /* 版本缺失哨兵，带此值的请求一律拒绝 */

/** 升级请求记录的落盘布局，整体占据 NVS 起始处的连续 92 字节 */
typedef struct
{
    uint32_t magic;         /**< OTA_REQUEST_MAGIC */
    uint32_t version;       /**< 固件版本号，不允许为 OTA_REQUEST_VERSION_NONE */
    uint32_t target_slot;   /**< 目标槽，0 表示 Slot A，1 表示 Slot B */
    uint32_t image_size;    /**< 实收镜像字节数 */
    uint32_t image_crc32;   /**< 实收数据累计 CRC32 */
    uint8_t  pubkey_id;                             /**< 签名所用公钥编号 */
    uint8_t  sig_algorithm;                         /**< 签名算法标识，取自 OTA_IMAGE_SIG_* */
    uint8_t  reserved[2];                           /**< 预留，必须写 0 */
    uint8_t  signature[OTA_IMAGE_SIGNATURE_SIZE];   /**< Ed25519 签名，透传给 Boot 验签 */
    uint32_t record_crc32;  /**< 记录 CRC32，覆盖本字段之前的 88 字节 */
} ota_request_t;

/**
 * @brief  填充一条升级请求并算好记录 CRC32
 * @param  request      待填充的记录
 * @param  version      固件版本号
 * @param  target_slot  目标槽，0 表示 Slot A，1 表示 Slot B
 * @param  image_size   实收镜像字节数
 * @param  image_crc32  实收数据累计 CRC32
 * @param  sig_algorithm 签名算法标识，取自 OTA_IMAGE_SIG_*
 * @param  pubkey_id    签名所用公钥编号
 * @param  signature    签名指针，长度 OTA_IMAGE_SIGNATURE_SIZE 字节；传 NULL 表示
 *                      未签名，此时签名域清零且 sig_algorithm 被改写为 SIG_NONE
 * @note   record_crc32 由本函数最后算出，调用方不得自行填写。签名字段参与记录
 *         完整性校验，但记录 CRC32 只证明"记录自身没被写坏"，不构成任何信任来源；
 *         固件是否可信由 Boot 侧用内建公钥验签决定。
 */
void ota_request_build(ota_request_t *request,
                       uint32_t version,
                       uint32_t target_slot,
                       uint32_t image_size,
                       uint32_t image_crc32,
                       uint8_t sig_algorithm,
                       uint8_t pubkey_id,
                       const uint8_t *signature);

/**
 * @brief  校验一条升级请求的结构自洽性
 * @param  request 待校验的记录
 * @return true 表示魔术、版本、长度与记录 CRC32 全部通过
 * @retval false 任一项不符，半截记录与陈旧内容都会在这里被挡下
 * @note   只校验"记录自己说的话是否自洽"，不重算槽内 CRC32，也不判断槽是否
 *         可写——那两件事依赖 Metadata 与 Flash 布局，属于 Boot 侧的逻辑。
 */
bool ota_request_is_valid(const ota_request_t *request);

#endif /* OTA_REQUEST_H */
