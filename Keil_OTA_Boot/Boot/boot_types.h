/**
 ******************************************************************************
 * @file    boot_types.h
 * @brief   Bootloader 公共数据类型
 ******************************************************************************
 */
#ifndef BOOT_TYPES_H
#define BOOT_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    BOOT_STATUS_OK = 0,
    BOOT_STATUS_IDLE,
    BOOT_STATUS_INVALID_PARAM,
    BOOT_STATUS_INVALID_STATE,
    BOOT_STATUS_INVALID_IMAGE,
    BOOT_STATUS_FLASH_ERROR,
    BOOT_STATUS_METADATA_ERROR,
    BOOT_STATUS_PROTOCOL_ERROR,
    BOOT_STATUS_CRC_ERROR,
    BOOT_STATUS_TIMEOUT,
    BOOT_STATUS_OUT_OF_RANGE,
    BOOT_STATUS_SEQUENCE_ERROR,
    BOOT_STATUS_JUMP_RETURNED
} boot_status_t;

typedef enum
{
    BOOT_SLOT_ID_A = 0,
    BOOT_SLOT_ID_B = 1,
    BOOT_SLOT_ID_NONE = 0xFFu
} boot_slot_id_t;

typedef enum
{
    BOOT_IMAGE_STATE_EMPTY = 0,
    BOOT_IMAGE_STATE_VALID,
    BOOT_IMAGE_STATE_PENDING,
    BOOT_IMAGE_STATE_CONFIRMED,
    BOOT_IMAGE_STATE_INVALID
} boot_image_state_t;

typedef struct
{
    boot_slot_id_t id;
    uint32_t address;
    uint32_t size;
} boot_slot_t;

typedef struct
{
    uint32_t magic;
    uint32_t header_version;
    uint32_t header_size;
    uint32_t hardware_id;
    uint32_t firmware_version;
    uint32_t image_size;
    uint32_t load_address;
    uint32_t image_crc32;
} boot_image_header_t;

typedef struct
{
    uint32_t state;
    uint32_t version;
    uint32_t image_size;
    uint32_t image_crc32;
} boot_slot_record_t;

typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t record_size;
    uint32_t sequence;
    uint32_t active_slot;
    uint32_t confirmed_slot;
    uint32_t pending_slot;
    uint32_t boot_attempts;
    boot_slot_record_t slot_a;
    boot_slot_record_t slot_b;
    uint32_t record_crc32;
} boot_metadata_t;

#endif /* BOOT_TYPES_H */
