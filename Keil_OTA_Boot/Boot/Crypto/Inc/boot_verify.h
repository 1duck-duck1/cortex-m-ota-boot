/**
 ******************************************************************************
 * @file    boot_verify.h
 * @brief   槽内镜像的 Ed25519 验签封装
 *
 * Boot 只信"被内建公钥验证过"的镜像。验签的对象不是整段固件，而是一个约定好的
 * 摘要：digest = SHA-512(prologue ‖ 载荷)。prologue 是镜像头前 28 字节
 * （magic..version，即 OTA_IMAGE_SIGNED_PREFIX_SIZE），排除了 header_crc32 与签名域
 * 本身，既避免自指，又让 hw_id/version 一并进入被签内容。PC 侧必须按同一字段顺序
 * 与长度拼出该前缀后签名，两侧逐字节对齐。
 *
 * 设备侧只做验签：公钥固化在本镜像里，私钥不出签名机，因此 Boot 没有任何
 * 伪造签名的能力。
 ******************************************************************************
 */
#ifndef BOOT_VERIFY_H
#define BOOT_VERIFY_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_image.h"

/**
 * @brief  验签一个槽内镜像
 * @param  slot_base  槽起始地址
 * @param  header     已通过头校验的镜像头，从其中取载荷长度与签名算法
 * @param  signature  待验签名，长度 OTA_IMAGE_SIGNATURE_SIZE 字节
 * @param  pubkey_id  签名所用公钥编号，用于在公钥表里查公钥
 * @return true 表示签名有效；BOOT_REQUIRE_SIGNATURE=0 的开发模式下恒为 true
 * @retval false 槽地址非法、签名缺失、算法不受支持、公钥未知，或验签不通过
 * @note   本函数不校验 header 自身与载荷 CRC32，调用方必须先做那两道关，
 *         否则会为一个内容已损坏的槽消耗一次 SHA-512 全文扫描。
 *         载荷从 slot_base + OTA_IMAGE_LOAD_OFFSET 起 header->image_size 字节，
 *         分块读取后流式送入哈希，不在 RAM 里缓存整段固件。
 */
bool boot_verify_image(uint32_t slot_base,
                       const ota_image_header_t *header,
                       const uint8_t signature[OTA_IMAGE_SIGNATURE_SIZE],
                       uint8_t pubkey_id);

/**
 * @brief  查询当前构建是否强制要求签名
 * @return true 表示 BOOT_REQUIRE_SIGNATURE 已打开，未签名镜像一律拒绝
 * @retval false 表示开发模式，验签被跳过、未签名镜像被放行
 */
bool boot_verify_required(void);

#endif /* BOOT_VERIFY_H */
