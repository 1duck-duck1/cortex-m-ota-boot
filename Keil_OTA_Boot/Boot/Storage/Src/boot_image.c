/**
 ******************************************************************************
 * @file    boot_image.c
 * @brief   槽内镜像的完整校验实现
 ******************************************************************************
 */
#include "boot_image.h"

#include <string.h>

#include "boot_conf.h"
#include "boot_flash.h"
#include "ota_crc32.h"
#include "ota_request.h"

/** 重算载荷 CRC32 时每次搬运的字节数，取 128 是为了让栈占用可忽略 */
#define BOOT_IMAGE_CRC_CHUNK   (128u)

/**
 * @brief  计算某个槽载荷的 CRC32
 * @param  base       槽起始地址
 * @param  image_size 载荷长度，单位字节
 * @param  crc32_out  计算结果
 * @return true 表示计算完成
 * @note   分块搬运后交给 ota_crc32_update，避免为了 384 KB 在栈上开缓冲区。
 */
static bool boot_image_crc_of(uint32_t base, uint32_t image_size, uint32_t *crc32_out)
{
    uint8_t buffer[BOOT_IMAGE_CRC_CHUNK];
    uint32_t address = base + OTA_IMAGE_LOAD_OFFSET;
    uint32_t remaining = image_size;
    uint32_t chunk;
    uint32_t crc = OTA_CRC32_INIT_VALUE;

    while (remaining > 0u)
    {
        chunk = remaining;
        if (chunk > (uint32_t)BOOT_IMAGE_CRC_CHUNK)
        {
            chunk = (uint32_t)BOOT_IMAGE_CRC_CHUNK;
        }

        if (!boot_flash_read(address, buffer, chunk))
        {
            return false;
        }

        crc = ota_crc32_update(crc, buffer, chunk);
        address += chunk;
        remaining -= chunk;
    }

    *crc32_out = ota_crc32_final(crc);

    return true;
}

/**
 * @brief  判断某个槽是否从未被写过
 * @param  base 槽起始地址
 * @return true 表示槽首 4 字节仍为擦除态
 */
static bool boot_image_is_empty(uint32_t base)
{
    return boot_flash_is_erased(base, 4u);
}

boot_image_state_t boot_image_check(uint32_t slot_id, boot_image_t *image)
{
    ota_image_header_t header;
    boot_image_t local;
    uint32_t base = boot_flash_slot_base(slot_id);

    local.state = BOOT_IMAGE_HEADER_INVALID;
    local.image_size = 0u;
    local.payload_crc32 = 0u;
    local.version = OTA_REQUEST_VERSION_NONE;

    /* slot_id 非法属于调用方的编程错误，按"头不可信"处理，绝不返回 VALID。 */
    if (base == 0u)
    {
        if (image != NULL)
        {
            *image = local;
        }
        return BOOT_IMAGE_HEADER_INVALID;
    }

    if (boot_image_is_empty(base))
    {
        local.state = BOOT_IMAGE_EMPTY;
        if (image != NULL)
        {
            *image = local;
        }
        return BOOT_IMAGE_EMPTY;
    }

    if (!boot_flash_read(base, (uint8_t *)&header, (uint32_t)sizeof(header)) ||
        !ota_image_header_verify(&header, BOOT_SLOT_SIZE))
    {
        if (image != NULL)
        {
            *image = local;
        }
        return BOOT_IMAGE_HEADER_INVALID;
    }

    local.image_size = header.image_size;
    local.version = header.version;

    /* 第二关：重算载荷 CRC32，与头里声明的一致才说明槽内容是完整的一份镜像。 */
    if (!boot_image_crc_of(base, header.image_size, &local.payload_crc32) ||
        (local.payload_crc32 != header.payload_crc32))
    {
        local.state = BOOT_IMAGE_PAYLOAD_MISMATCH;
        if (image != NULL)
        {
            *image = local;
        }
        return BOOT_IMAGE_PAYLOAD_MISMATCH;
    }

    /* 第三关：向量表首两项必须能启动，否则跳过去立刻跑飞。 */
    if (!boot_image_vector_valid(base + OTA_IMAGE_LOAD_OFFSET,
                                 base,
                                 header.image_size,
                                 NULL,
                                 NULL))
    {
        local.state = BOOT_IMAGE_VECTOR_INVALID;
        if (image != NULL)
        {
            *image = local;
        }
        return BOOT_IMAGE_VECTOR_INVALID;
    }

    local.state = BOOT_IMAGE_VALID;
    if (image != NULL)
    {
        *image = local;
    }

    return BOOT_IMAGE_VALID;
}

bool boot_image_measure(uint32_t slot_id, uint32_t *image_size)
{
    uint32_t base = boot_flash_slot_base(slot_id);
    uint32_t start;
    uint32_t end;
    uint32_t length;
    uint32_t index;

    if ((base == 0u) || (image_size == NULL))
    {
        return false;
    }

    if (boot_image_is_empty(base))
    {
        return false;
    }

    start = base + OTA_IMAGE_LOAD_OFFSET;
    end = base + BOOT_SLOT_SIZE;

    /* 从槽尾向前找最后一个非 0xFF 字节：Keil 直接烧录的产物没有长度信息，
       这是唯一能反推载荷边界的手段。长度必须从载荷起点量起，而不是从槽尾
       量起——从槽尾量出来的是"槽内剩余空间"，会大出一个数量级。 */
    length = 0u;
    for (index = 0u; index < (BOOT_SLOT_SIZE - OTA_IMAGE_LOAD_OFFSET); index++)
    {
        if (*(volatile uint8_t *)(end - 1u - index) != 0xFFu)
        {
            length = (end - index) - start;
            break;
        }
    }

    if (length == 0u)
    {
        return false;
    }

    /* 编译产物必然按字对齐，向上取整到 4 字节。 */
    if ((length & 0x3u) != 0u)
    {
        length = (length + 3u) & ~0x3u;
    }

    if (length > (BOOT_SLOT_SIZE - OTA_IMAGE_LOAD_OFFSET))
    {
        return false;
    }

    /* 反推出的长度要能通过向量表检查，否则等于给一个跑不起来的槽补头。 */
    if (!boot_image_vector_valid(start, base, length, NULL, NULL))
    {
        return false;
    }

    *image_size = length;

    return true;
}

bool boot_image_payload_crc(uint32_t slot_id, uint32_t image_size, uint32_t *crc32_out)
{
    uint32_t base = boot_flash_slot_base(slot_id);

    if ((base == 0u) || (crc32_out == NULL) ||
        (image_size == 0u) || (image_size > (BOOT_SLOT_SIZE - OTA_IMAGE_LOAD_OFFSET)))
    {
        return false;
    }

    return boot_image_crc_of(base, image_size, crc32_out);
}

bool boot_image_write_header(uint32_t slot_id,
                             uint32_t version,
                             uint32_t image_size,
                             uint32_t payload_crc32)
{
    ota_image_header_t header;
    uint32_t base = boot_flash_slot_base(slot_id);

    if ((base == 0u) || (image_size == 0u) ||
        (image_size > (BOOT_SLOT_SIZE - OTA_IMAGE_LOAD_OFFSET)))
    {
        return false;
    }

    /* 只允许补写从未写过的头区：已写过的头必须整槽重传，否则新旧两次编程
       会按位相与，得到一份两个来源都不是的头。 */
    if (!boot_flash_is_erased(base, OTA_IMAGE_HEADER_SIZE))
    {
        return false;
    }

    (void)memset(&header, 0, sizeof(header));
    ota_image_header_fill(&header, OTA_IMAGE_HW_ID, version, image_size, payload_crc32);

    if (!boot_flash_write(base, (const uint8_t *)&header, (uint32_t)sizeof(header)))
    {
        return false;
    }

    /* 回读校验：Flash 编程失败未必返回错误，读回来对不上就不能认。 */
    (void)memset(&header, 0, sizeof(header));
    if (!boot_flash_read(base, (uint8_t *)&header, (uint32_t)sizeof(header)))
    {
        return false;
    }

    return ota_image_header_verify(&header, BOOT_SLOT_SIZE);
}

bool boot_image_vector_valid(uint32_t vector_address,
                             uint32_t slot_base,
                             uint32_t image_size,
                             uint32_t *sp_out,
                             uint32_t *pc_out)
{
    uint32_t sp;
    uint32_t pc;
    uint32_t entry;

    if (!boot_flash_read(vector_address, (uint8_t *)&sp, 4u) ||
        !boot_flash_read(vector_address + 4u, (uint8_t *)&pc, 4u))
    {
        return false;
    }

    if (sp_out != NULL)
    {
        *sp_out = sp;
    }
    if (pc_out != NULL)
    {
        *pc_out = pc;
    }

    /* SP 必须落在 SRAM 内且字对齐：等号取到 BOOT_RAM_END 是合法的空栈初值。 */
    if (((sp & 0x3u) != 0u) || (sp < BOOT_RAM_BASE) || (sp > BOOT_RAM_END))
    {
        return false;
    }

    /* PC 是 Thumb 地址，最低位为 1；去掉该位后必须落在本槽载荷区间内。 */
    if ((pc & 0x1u) == 0u)
    {
        return false;
    }

    entry = pc & ~0x1u;
    if (entry < vector_address)
    {
        return false;
    }
    if (image_size == 0u)
    {
        return false;
    }
    if (entry >= (vector_address + image_size))
    {
        return false;
    }

    if ((slot_base == 0u) || (vector_address != (slot_base + OTA_IMAGE_LOAD_OFFSET)))
    {
        return false;
    }

    return true;
}

const char *boot_image_state_name(boot_image_state_t state)
{
    switch (state)
    {
        case BOOT_IMAGE_EMPTY:
            return "EMPTY";
        case BOOT_IMAGE_HEADER_INVALID:
            return "HEADER_INVALID";
        case BOOT_IMAGE_PAYLOAD_MISMATCH:
            return "PAYLOAD_MISMATCH";
        case BOOT_IMAGE_VECTOR_INVALID:
            return "VECTOR_INVALID";
        case BOOT_IMAGE_VALID:
            return "VALID";
        default:
            return "UNKNOWN";
    }
}
