#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include "boot_types.h"

boot_status_t boot_metadata_load(boot_metadata_t *metadata);
boot_status_t boot_metadata_commit(const boot_metadata_t *metadata);
boot_status_t boot_metadata_mark_pending(boot_metadata_t *metadata,
                                         boot_slot_id_t slot_id,
                                         uint32_t version,
                                         uint32_t image_size,
                                         uint32_t image_crc32);
boot_status_t boot_metadata_confirm(boot_metadata_t *metadata,
                                    boot_slot_id_t slot_id);
boot_status_t boot_metadata_rollback(boot_metadata_t *metadata);

#endif /* BOOT_METADATA_H */
