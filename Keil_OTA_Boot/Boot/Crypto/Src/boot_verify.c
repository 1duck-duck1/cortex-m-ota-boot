/**
 ******************************************************************************
 * @file    boot_verify.c
 * @brief   槽内镜像 Ed25519 验签的实现
 ******************************************************************************
 */
#include "boot_verify.h"

#include "boot_conf.h"
#include "boot_flash.h"
#include "boot_pubkey.h"
#include "monocypher-ed25519.h"

/** 流式哈希时每次搬运的字节数，取 256 让栈占用与读取次数都保持温和 */
#define BOOT_VERIFY_CHUNK   (256u)

/** SHA-512 摘要长度，单位字节 */
#define BOOT_VERIFY_DIGEST_SIZE   (64u)

bool boot_verify_required(void)
{
#if (BOOT_REQUIRE_SIGNATURE == 0u)
    return false;
#else
    return true;
#endif
}

bool boot_verify_image(uint32_t slot_base,
                       const ota_image_header_t *header,
                       const uint8_t signature[OTA_IMAGE_SIGNATURE_SIZE],
                       uint8_t pubkey_id)
{
#if (BOOT_REQUIRE_SIGNATURE == 0u)
    /* 开发模式：跳过验签，让 ota adopt 与未签名镜像仍可流转。 */
    (void)slot_base;
    (void)header;
    (void)signature;
    (void)pubkey_id;

    return true;
#else
    crypto_sha512_ctx ctx;
    uint8_t digest[BOOT_VERIFY_DIGEST_SIZE];
    uint8_t buffer[BOOT_VERIFY_CHUNK];
    const uint8_t *pubkey = NULL;
    uint32_t address;
    uint32_t remaining;
    uint32_t chunk;

    if ((header == NULL) || (signature == NULL) || (slot_base == 0u)) {
        return false;
    }

    /* 要求签名却给了"未签名"，直接拒绝，绝不退化成 CRC32 放行。 */
    if (header->sig_algorithm == OTA_IMAGE_SIG_NONE) {
        return false;
    }
    if (header->sig_algorithm != OTA_IMAGE_SIG_ED25519) {
        return false;
    }
    if (!boot_pubkey_lookup(pubkey_id, &pubkey) || (pubkey == NULL)) {
        return false;
    }
    if ((header->image_size == 0u) ||
        (header->image_size > (BOOT_SLOT_SIZE - OTA_IMAGE_LOAD_OFFSET))) {
        return false;
    }

    /* digest = SHA-512(prologue ‖ 载荷)，prologue 取头前 OTA_IMAGE_SIGNED_PREFIX_SIZE
       （28）字节，与上位机显式拼出的 28 字节前缀逐字节对应。 */
    crypto_sha512_init(&ctx);
    crypto_sha512_update(&ctx, (const uint8_t *)header, OTA_IMAGE_SIGNED_PREFIX_SIZE);

    address = slot_base + OTA_IMAGE_LOAD_OFFSET;
    remaining = header->image_size;
    while (remaining > 0u) {
        chunk = (remaining > (uint32_t)BOOT_VERIFY_CHUNK) ?
                (uint32_t)BOOT_VERIFY_CHUNK : remaining;
        if (!boot_flash_read(address, buffer, chunk)) {
            return false;
        }

        crypto_sha512_update(&ctx, buffer, (size_t)chunk);
        address += chunk;
        remaining -= chunk;
    }
    crypto_sha512_final(&ctx, digest);

    /* crypto_ed25519_check() 返回 0 表示验签通过。 */
    return (crypto_ed25519_check(signature, pubkey, digest, sizeof(digest)) == 0);
#endif
}
