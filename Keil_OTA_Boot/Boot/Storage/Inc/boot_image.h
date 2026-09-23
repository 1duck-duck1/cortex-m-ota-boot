/**
 ******************************************************************************
 * @file    boot_image.h
 * @brief   槽内镜像的完整校验：镜像头、载荷 CRC32、向量表三道关
 *
 * 这是"某个槽能不能启动"的唯一判据，启动决策与 ota recv 收尾都走它，避免两处
 * 各写一套判据后慢慢分歧。三道关的取舍：
 * 1. 镜像头自洽——魔术、版本、几何、头 CRC32，全部是本地计算，代价最低；
 * 2. 载荷 CRC32 与头里声明的一致——真正证明"槽里这堆字节就是当初那一份"；
 * 3. 向量表的 SP / PC 合法——即使前两关都过，SP 指向天上或 PC 落在槽外，
 *    跳过去也是立刻跑飞，必须在跳转前挡住。
 ******************************************************************************
 */
#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_image.h"

/** 槽内镜像的校验结果，由弱到强排列 */
typedef enum
{
    BOOT_IMAGE_EMPTY = 0,           /**< 槽首为擦除态，从未烧录或已被擦除 */
    BOOT_IMAGE_HEADER_INVALID,      /**< 镜像头缺失、魔术不符或头 CRC32 不对 */
    BOOT_IMAGE_PAYLOAD_MISMATCH,    /**< 头自洽，但载荷 CRC32 与头声明不符 */
    BOOT_IMAGE_VECTOR_INVALID,      /**< 头与载荷 CRC32 都对，但向量表 SP/PC 非法 */
    BOOT_IMAGE_VALID                /**< 三道关全部通过，可以跳转 */
} boot_image_state_t;

/** 一次校验的完整结果 */
typedef struct
{
    boot_image_state_t state;   /**< 校验结论 */
    uint32_t image_size;        /**< 头的载荷长度，单位字节；头无效时为 0 */
    uint32_t payload_crc32;     /**< 实际重算出的载荷 CRC32 */
    uint32_t version;           /**< 头的固件版本号；头无效时为 OTA_REQUEST_VERSION_NONE */
} boot_image_t;

/**
 * @brief  完整校验一个槽
 * @param  slot_id 槽编号，BOOT_SLOT_ID_A / BOOT_SLOT_ID_B
 * @param  image   校验结果，允许为 NULL
 * @return 校验结论
 * @note   载荷 CRC32 要读完整个镜像，384 KB 约耗时十余毫秒；每次启动都会执行，
 *         这是"绝不跳到坏镜像上"的代价。
 */
boot_image_state_t boot_image_check(uint32_t slot_id, boot_image_t *image);

/**
 * @brief  从槽内容反推载荷边界
 * @param  slot_id     槽编号
 * @param  image_size  反推出的载荷长度，单位字节
 * @return true 表示槽首向量表可启动且找到了最后一个非 0xFF 字节
 * @retval false 槽为空，或槽首 SP/PC 非法
 * @note   从槽尾向前扫最后一个非 0xFF 字节，是"Keil 直接烧录、没有长度信息"
 *         这一场景下唯一可用的长度来源（ota adopt 专用）。
 *         长度按 4 字节向上取整，因为编译产物必然按字对齐。
 */
bool boot_image_measure(uint32_t slot_id, uint32_t *image_size);

/**
 * @brief  重算整个槽载荷的 CRC32
 * @param  slot_id    槽编号
 * @param  image_size 载荷长度，单位字节
 * @param  crc32_out  计算结果
 * @return true 表示计算完成
 * @retval false 槽编号或长度非法
 */
bool boot_image_payload_crc(uint32_t slot_id, uint32_t image_size, uint32_t *crc32_out);

/**
 * @brief  把镜像头写进槽首
 * @param  slot_id        槽编号
 * @param  version        固件版本号
 * @param  image_size     载荷长度，单位字节
 * @param  payload_crc32  载荷 CRC32
 * @return true 表示头已落盘且可被 boot_image_check() 认作自洽
 * @retval false 槽编号或长度非法，头区已被写过，或编程失败
 * @note   只允许在头区仍为擦除态时写入：已写过头区的槽必须整槽重传，
 *         否则新头会与旧头按位相与，得到一份两个来源都不是的头。
 */
bool boot_image_write_header(uint32_t slot_id,
                             uint32_t version,
                             uint32_t image_size,
                             uint32_t payload_crc32);

/**
 * @brief  校验一组向量表首两项是否可启动
 * @param  vector_address 向量表首地址，即槽首 + OTA_IMAGE_LOAD_OFFSET
 * @param  slot_base      槽起始地址，用于判定入口地址是否落在本槽内
 * @param  image_size     载荷长度，单位字节
 * @param  sp_out         初始 MSP，允许为 NULL
 * @param  pc_out         复位入口，允许为 NULL
 * @return true 表示 SP 落在 SRAM 内且 PC 落在本槽载荷区间内、为 Thumb 地址
 */
bool boot_image_vector_valid(uint32_t vector_address,
                             uint32_t slot_base,
                             uint32_t image_size,
                             uint32_t *sp_out,
                             uint32_t *pc_out);

/**
 * @brief  取校验结论的可读名称
 * @param  state 校验结论
 * @return 静态字符串，永不返回 NULL
 */
const char *boot_image_state_name(boot_image_state_t state);

#endif /* BOOT_IMAGE_H */
