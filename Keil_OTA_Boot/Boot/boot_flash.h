#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include "boot_types.h"

boot_status_t boot_flash_init(void);
boot_status_t boot_flash_erase_slot(boot_slot_id_t slot_id);
boot_status_t boot_flash_write(uint32_t address,
                               const uint8_t *data,
                               uint32_t size);
boot_status_t boot_flash_write_metadata(uint32_t address,
                                        const uint8_t *data,
                                        uint32_t size);
boot_status_t boot_flash_erase_metadata(uint32_t address);
boot_status_t boot_flash_read(uint32_t address,
                              uint8_t *data,
                              uint32_t size);
bool boot_flash_range_valid(uint32_t address, uint32_t size);

#endif /* BOOT_FLASH_H */
