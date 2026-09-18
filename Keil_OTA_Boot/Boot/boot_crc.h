#ifndef BOOT_CRC_H
#define BOOT_CRC_H

#include <stdint.h>

uint32_t boot_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size);
uint32_t boot_crc32(const uint8_t *data, uint32_t size);

#endif /* BOOT_CRC_H */
