#include "boot_crc.h"
#include <stddef.h>

uint32_t boot_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    if (data == NULL)
    {
        return crc;
    }

    for (uint32_t i = 0u; i < size; i++)
    {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++)
        {
            crc = ((crc & 1u) != 0u) ?
                  ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
        }
    }

    return crc;
}

uint32_t boot_crc32(const uint8_t *data, uint32_t size)
{
    return boot_crc32_update(0xFFFFFFFFu, data, size) ^ 0xFFFFFFFFu;
}
