/**
 ******************************************************************************
 * @file    boot_pubkey.h
 * @brief   固化在 Boot 镜像里的验签公钥表
 *
 * 公钥是整条信任链的根：它随 Boot 一起烧进 sector 0-1，任何要进槽、要启动的
 * 镜像都必须能被其中一把公钥验证。私钥只存在于签名机（PC 上位机），永不进入
 * 本工程，因此 Boot 侧只读公钥、只做验签，没有任何签名能力。
 *
 * 公钥表支持按编号查找：镜像头里的 pubkey_id 在验签前先在这里过一遍，查不到
 * 编号的镜像直接判为不可信。当前只登记编号 1 一把公钥，未来轮换时在此追加。
 ******************************************************************************
 */
#ifndef BOOT_PUBKEY_H
#define BOOT_PUBKEY_H

#include <stdbool.h>
#include <stdint.h>

/** Ed25519 公钥长度，单位字节 */
#define BOOT_PUBKEY_SIZE    (32u)

/**
 * @brief  按编号查内置公钥
 * @param  id  公钥编号，取自镜像头的 pubkey_id
 * @param  key 查到的公钥指针，指向 32 字节常量；查不到时置 NULL
 * @return true 表示编号已登记
 * @retval false 编号未知，key 被置 NULL
 * @note   返回值只说明"编号存在"，不代表公钥本身可信——公钥的可信来自它随
 *         Boot 镜像一起被烧录这一事实，与运行期无关。
 */
bool boot_pubkey_lookup(uint8_t id, const uint8_t **key);

/** 编号 1 的 Ed25519 公钥；由 PC 上位机 --genkey 输出的十六进制粘贴填入 */
extern const uint8_t boot_pubkey_ed25519[BOOT_PUBKEY_SIZE];

#endif /* BOOT_PUBKEY_H */
