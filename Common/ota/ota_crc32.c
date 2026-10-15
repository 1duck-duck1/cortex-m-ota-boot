/**
 ******************************************************************************
 * @file    ota_crc32.c
 * @brief   CRC32 增量计算的查表实现
 ******************************************************************************
 */
#include "ota_crc32.h"

/**
 * 低 4 位查表：64 字节常量换掉 4 次移位，整槽 384 KB 校验耗时降到毫秒级。
 * 表项为反射多项式 0xEDB88320 的 4 位展开，不可替换为 8 位表以外的自造值。
 */
static const uint32_t s_crc32_nibble_table[16] =
{
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    uint32_t index;
    uint32_t value;

    for (index = 0u; index < size; index++) {
        value = crc ^ (uint32_t)data[index];
        crc = (value >> 4u) ^ s_crc32_nibble_table[value & 0x0Fu];
        crc = (crc >> 4u) ^ s_crc32_nibble_table[crc & 0x0Fu];
    }

    return crc;
}

uint32_t ota_crc32_final(uint32_t crc)
{
    return crc ^ OTA_CRC32_XOROUT_VALUE;
}

uint32_t ota_crc32_calc(const uint8_t *data, uint32_t size)
{
    return ota_crc32_final(ota_crc32_update(OTA_CRC32_INIT_VALUE, data, size));
}
