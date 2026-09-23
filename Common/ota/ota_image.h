/**
 ******************************************************************************
 * @file    ota_image.h
 * @brief   槽内镜像头：结构定义、填充与校验
 *
 * 每个槽的开头 0x200 字节固定是镜像头，真正的固件从槽首 + 0x200 开始链接与
 * 运行，因此槽首既是一个"自描述头"，也是天然的 VTOR 对齐缓冲：
 * SCB_VTOR_TBLOFF_Msk 会截断低 7 位，只有 0x200 对齐才能保证 VTOR 落在
 * 向量表上（64 字节头曾被证明会截断出错）。
 *
 * 头区剩余字节保持擦除态 0xFF：header_crc32 只覆盖结构体本身
 * （sizeof(ota_image_header_t) 字节），与填充内容无关。
 ******************************************************************************
 */
#ifndef OTA_IMAGE_H
#define OTA_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#define OTA_IMAGE_MAGIC            (0x544F4F42u)   /* ASCII "BOOT"，小端落在槽首最前 4 字节 */
#define OTA_IMAGE_HEADER_SIZE      (0x200u)        /* 头长度，同时是载荷偏移与 VTOR 对齐单位 */
#define OTA_IMAGE_LOAD_OFFSET      (OTA_IMAGE_HEADER_SIZE)  /* 载荷相对槽首的偏移，恒等于头长度 */
#define OTA_IMAGE_HEADER_VERSION   (1u)            /* 头结构版本，结构变动时必须递增 */
#define OTA_IMAGE_HW_ID            (0x0407u)       /* 目标硬件标识，防止跨板镜像被启动 */

/** 镜像头的内存布局，写进槽首 0x200 字节的最前面 */
typedef struct
{
    uint32_t magic;             /**< OTA_IMAGE_MAGIC */
    uint16_t hw_id;             /**< 目标硬件标识，取自 OTA_IMAGE_HW_ID */
    uint16_t header_version;    /**< 头结构版本，取自 OTA_IMAGE_HEADER_VERSION */
    uint32_t header_size;       /**< 头长度，单位字节，恒为 OTA_IMAGE_HEADER_SIZE */
    uint32_t load_offset;       /**< 载荷偏移，单位字节，恒为 OTA_IMAGE_LOAD_OFFSET */
    uint32_t image_size;        /**< 载荷长度，单位字节，必须落在 (0, 槽容量 - 偏移] */
    uint32_t payload_crc32;     /**< 载荷 CRC32，覆盖槽首 + load_offset 起 image_size 字节 */
    uint32_t version;           /**< 固件版本号，防回滚比较的依据 */
    uint32_t header_crc32;      /**< 头 CRC32，计算时本字段按 0 参与 */
    uint32_t reserved[4];       /**< 预留，必须写 0 */
} ota_image_header_t;

/**
 * @brief  计算镜像头的 CRC32
 * @param  header 待计算的镜像头
 * @return CRC32，与 header_crc32 字段比较
 * @note   本字段自身按 0 参与计算，覆盖范围是结构体的前
 *         sizeof(ota_image_header_t) 字节，与头区其余填充字节无关。
 */
uint32_t ota_image_header_crc(const ota_image_header_t *header);

/**
 * @brief  填充镜像头并算好头 CRC32
 * @param  header        待填充的镜像头，调用方需保证其可写
 * @param  hw_id         目标硬件标识
 * @param  version       固件版本号
 * @param  image_size    载荷长度，单位字节
 * @param  payload_crc32 载荷 CRC32
 * @note   不写头区剩余字节：它们在槽里保持 0xFF，不影响头校验。
 */
void ota_image_header_fill(ota_image_header_t *header,
                           uint16_t hw_id,
                           uint32_t version,
                           uint32_t image_size,
                           uint32_t payload_crc32);

/**
 * @brief  校验镜像头的自洽性
 * @param  header    待校验的镜像头
 * @param  slot_size 所在槽的容量，单位字节
 * @return true 表示魔术、版本、几何与头 CRC32 全部通过
 * @retval false 任一项不符，此时载荷长度与 CRC32 都不可信
 * @note   只做"头自己说的话是否自洽"的判断，不读载荷、不算载荷 CRC32；
 *         载荷是否完好由调用方另行计算后比对 payload_crc32。
 */
bool ota_image_header_verify(const ota_image_header_t *header, uint32_t slot_size);

#endif /* OTA_IMAGE_H */
