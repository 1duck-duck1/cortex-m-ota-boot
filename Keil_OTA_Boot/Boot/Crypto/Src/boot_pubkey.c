/**
 ******************************************************************************
 * @file    boot_pubkey.c
 * @brief   内置验签公钥表的实现
 ******************************************************************************
 */
#include "boot_pubkey.h"

#include "ota_image.h"

/*
 * 编号 1 的 Ed25519 公钥（32 字节），对应签名机上的私钥 signing.key。
 *
 * 由 PC 上位机 `固件升级上位机.exe --genkey signing.key` 生成；私钥种子留在签名机，
 * 公钥以字节数组固化于此。私钥一旦更换，必须同步替换本数组并重新编译 Boot，
 * 否则新旧镜像都验不过。
 *
 * 注意：本数组是 32 个字节，不是大整数；顺序即为 --genkey 打印的十六进制顺序。
 */
const uint8_t boot_pubkey_ed25519[BOOT_PUBKEY_SIZE] =
{
    0x7Bu, 0x14u, 0xC9u, 0x78u, 0x5Bu, 0xF3u, 0x97u, 0xAAu,
    0x98u, 0x3Eu, 0x0Bu, 0x3Au, 0x97u, 0xECu, 0x4Bu, 0xE4u,
    0x0Fu, 0xE1u, 0xB6u, 0x39u, 0x9Cu, 0xD7u, 0xB7u, 0x91u,
    0x36u, 0x9Bu, 0xD4u, 0x29u, 0x12u, 0x6Au, 0x2Cu, 0x88u
};

bool boot_pubkey_lookup(uint8_t id, const uint8_t **key)
{
    if (key == NULL) {
        return false;
    }

    if (id == OTA_IMAGE_PUBKEY_ID) {
        *key = boot_pubkey_ed25519;
        return true;
    }

    *key = NULL;

    return false;
}
