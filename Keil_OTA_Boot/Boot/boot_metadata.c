#include "boot_metadata.h"
#include "boot_conf.h"
#include "boot_crc.h"
#include "boot_flash.h"
#include <string.h>
#include <stddef.h>

static uint32_t boot_metadata_crc(const boot_metadata_t *metadata)
{
    return boot_crc32((const uint8_t *)metadata,
                      (uint32_t)offsetof(boot_metadata_t, record_crc32));
}

static bool boot_metadata_valid(const boot_metadata_t *metadata)
{
    if (metadata->magic != BOOT_METADATA_MAGIC ||
        metadata->format_version != BOOT_METADATA_VERSION ||
        metadata->record_size != sizeof(boot_metadata_t))
    {
        return false;
    }
    return metadata->record_crc32 == boot_metadata_crc(metadata);
}

static boot_status_t boot_metadata_read_copy(uint32_t address,
                                             boot_metadata_t *metadata)
{
    boot_status_t status = boot_flash_read(address,
                                           (uint8_t *)metadata,
                                           sizeof(*metadata));
    if ((status != BOOT_STATUS_OK) || !boot_metadata_valid(metadata))
    {
        return BOOT_STATUS_METADATA_ERROR;
    }
    return BOOT_STATUS_OK;
}

boot_status_t boot_metadata_load(boot_metadata_t *metadata)
{
    boot_metadata_t copy_a;
    boot_metadata_t copy_b;
    boot_status_t status_a;
    boot_status_t status_b;

    if (metadata == NULL)
    {
        return BOOT_STATUS_INVALID_PARAM;
    }

    status_a = boot_metadata_read_copy(BOOT_META_A_ADDR, &copy_a);
    status_b = boot_metadata_read_copy(BOOT_META_B_ADDR, &copy_b);

    if ((status_a != BOOT_STATUS_OK) && (status_b != BOOT_STATUS_OK))
    {
        memset(metadata, 0, sizeof(*metadata));
        metadata->magic = BOOT_METADATA_MAGIC;
        metadata->format_version = BOOT_METADATA_VERSION;
        metadata->record_size = sizeof(*metadata);
        metadata->active_slot = BOOT_SLOT_ID_A;
        metadata->confirmed_slot = BOOT_SLOT_ID_A;
        metadata->pending_slot = BOOT_SLOT_ID_NONE;
        metadata->slot_a.state = BOOT_IMAGE_STATE_VALID;
        metadata->sequence = 0u;
        metadata->record_crc32 = boot_metadata_crc(metadata);
        return BOOT_STATUS_METADATA_ERROR;
    }

    if ((status_a == BOOT_STATUS_OK) &&
        ((status_b != BOOT_STATUS_OK) ||
         (copy_a.sequence >= copy_b.sequence)))
    {
        *metadata = copy_a;
    }
    else
    {
        *metadata = copy_b;
    }
    return BOOT_STATUS_OK;
}

boot_status_t boot_metadata_commit(const boot_metadata_t *metadata)
{
    boot_metadata_t record;
    uint32_t target_address;
    boot_status_t status;

    if (metadata == NULL)
    {
        return BOOT_STATUS_INVALID_PARAM;
    }

    record = *metadata;
    record.magic = BOOT_METADATA_MAGIC;
    record.format_version = BOOT_METADATA_VERSION;
    record.record_size = sizeof(record);
    record.record_crc32 = boot_metadata_crc(&record);

    target_address = ((record.sequence & 1u) == 0u) ?
                     BOOT_META_A_ADDR : BOOT_META_B_ADDR;

    /* 当前实现只提交到一份新副本，保留另一份旧副本。 */
    {
        status = boot_flash_erase_metadata(target_address);
        if (status != BOOT_STATUS_OK)
        {
            return status;
        }
    }

    status = boot_flash_write_metadata(target_address,
                                       (const uint8_t *)&record,
                                       sizeof(record));
    if (status != BOOT_STATUS_OK)
    {
        return status;
    }

    return boot_metadata_read_copy(target_address, &record);
}

static boot_slot_record_t *boot_metadata_slot_record(boot_metadata_t *metadata,
                                                     boot_slot_id_t slot_id)
{
    return (slot_id == BOOT_SLOT_ID_A) ? &metadata->slot_a : &metadata->slot_b;
}

boot_status_t boot_metadata_mark_pending(boot_metadata_t *metadata,
                                         boot_slot_id_t slot_id,
                                         uint32_t version,
                                         uint32_t image_size,
                                         uint32_t image_crc32)
{
    boot_slot_record_t *record;
    if ((metadata == NULL) ||
        ((slot_id != BOOT_SLOT_ID_A) && (slot_id != BOOT_SLOT_ID_B)))
    {
        return BOOT_STATUS_INVALID_PARAM;
    }
    record = boot_metadata_slot_record(metadata, slot_id);
    record->state = BOOT_IMAGE_STATE_PENDING;
    record->version = version;
    record->image_size = image_size;
    record->image_crc32 = image_crc32;
    metadata->pending_slot = slot_id;
    metadata->sequence++;
    return boot_metadata_commit(metadata);
}

boot_status_t boot_metadata_confirm(boot_metadata_t *metadata,
                                    boot_slot_id_t slot_id)
{
    boot_slot_record_t *record;
    if ((metadata == NULL) ||
        ((slot_id != BOOT_SLOT_ID_A) && (slot_id != BOOT_SLOT_ID_B)))
    {
        return BOOT_STATUS_INVALID_PARAM;
    }
    record = boot_metadata_slot_record(metadata, slot_id);
    record->state = BOOT_IMAGE_STATE_CONFIRMED;
    metadata->active_slot = slot_id;
    metadata->confirmed_slot = slot_id;
    metadata->pending_slot = BOOT_SLOT_ID_NONE;
    metadata->boot_attempts = 0u;
    metadata->sequence++;
    return boot_metadata_commit(metadata);
}

boot_status_t boot_metadata_rollback(boot_metadata_t *metadata)
{
    if (metadata == NULL)
    {
        return BOOT_STATUS_INVALID_PARAM;
    }
    metadata->active_slot = metadata->confirmed_slot;
    metadata->pending_slot = BOOT_SLOT_ID_NONE;
    metadata->boot_attempts = 0u;
    metadata->sequence++;
    return boot_metadata_commit(metadata);
}
