/**
 ******************************************************************************
 * @file    ota_ymodem.c
 * @brief   YMODEM 接收侧组帧与文件信息包解析实现
 ******************************************************************************
 */
#include "ota_ymodem.h"

#include <string.h>

#define OTA_YMODEM_INDEX_SUM        (0xFFu)   /* 序号与其反码之和，恒为 0xFF */

/**
 * @brief  把状态机拉回找帧首的阶段
 * @param  rx 接收状态机
 */
static void ota_ymodem_rx_reset_frame(ota_ymodem_rx_t *rx)
{
    rx->state = OTA_YMODEM_RX_HEADER;
    rx->payload_size = 0u;
    rx->position = 0u;
    rx->crc_calc = 0u;
    rx->crc_received = 0u;
}

/**
 * @brief  判断字符是否为空白分隔符
 * @param  value 待判断字符
 * @return true 表示空格或制表符
 */
static bool ota_ymodem_is_space(uint8_t value)
{
    return ((value == (uint8_t)' ') || (value == (uint8_t)'\t'));
}

/**
 * @brief  判断字符是否为十六进制数字
 * @param  value 待判断字符
 * @param  digit 解析出的数值
 * @return true 表示是合法的十六进制数字
 */
static bool ota_ymodem_hex_digit(uint8_t value, uint32_t *digit)
{
    if ((value >= (uint8_t)'0') && (value <= (uint8_t)'9')) {
        *digit = (uint32_t)(value - (uint8_t)'0');
        return true;
    }
    if ((value >= (uint8_t)'a') && (value <= (uint8_t)'f')) {
        *digit = (uint32_t)(value - (uint8_t)'a') + 10u;
        return true;
    }
    if ((value >= (uint8_t)'A') && (value <= (uint8_t)'F')) {
        *digit = (uint32_t)(value - (uint8_t)'A') + 10u;
        return true;
    }

    return false;
}

/**
 * @brief  解析第二字段里的版本号令牌
 * @param  data  第二字段首字节
 * @param  size  第二字段长度，单位字节
 * @param  info  解析结果
 * @note   只有带 0x / 0X 前缀的令牌才是版本号，其余（标准 mtime）一律跳过。
 */
static void ota_ymodem_scan_version(const uint8_t *data,
                                    uint32_t size,
                                    ota_ymodem_file_info_t *info)
{
    uint32_t position = 0u;
    uint32_t digit;
    uint32_t value;
    uint32_t digits;
    bool overflow;

    while (position < size) {
        if (!ota_ymodem_is_space(data[position])) {
            break;
        }
        position++;
    }

    while (position < size) {
        if (ota_ymodem_is_space(data[position])) {
            position++;
            continue;
        }

        if (((data[position] == (uint8_t)'0') && ((position + 1u) < size)) &&
            ((data[position + 1u] == (uint8_t)'x') || (data[position + 1u] == (uint8_t)'X'))) {
            position += 2u;
            value = 0u;
            digits = 0u;
            overflow = false;
            while ((position < size) && ota_ymodem_hex_digit(data[position], &digit)) {
                if ((value & 0xF0000000u) != 0u) {
                    overflow = true;
                }
                value = (value << 4u) | digit;
                digits++;
                position++;
            }
            if ((digits > 0u) && (!overflow)) {
                info->version = value;
                info->has_version = true;
                return;
            }
            continue;
        }

        while ((position < size) && (!ota_ymodem_is_space(data[position]))) {
            position++;
        }
    }
}

/**
 * @brief  解析第二字段里的签名令牌
 * @param  data  第二字段首字节
 * @param  size  第二字段长度，单位字节
 * @param  info  解析结果
 * @note   只识别 sig= 与 kid= 两种前缀；sig= 必须恰好 OTA_YMODEM_SIGNATURE_HEX_LEN
 *         个十六进制字符，kid= 认 1~2 位十六进制。任一令牌格式非法都只忽略该项，
 *         不置 has_signature，也不影响长度与文件名的解析结果。
 */
static void ota_ymodem_scan_signature(const uint8_t *data,
                                      uint32_t size,
                                      ota_ymodem_file_info_t *info)
{
    uint32_t position = 0u;
    uint32_t token_start;
    uint32_t token_end;
    uint32_t length;
    uint32_t index;
    uint32_t digit;
    uint32_t high;
    uint32_t low;
    uint32_t value;
    uint32_t digits;
    bool valid;

    while (position < size) {
        while ((position < size) && ota_ymodem_is_space(data[position])) {
            position++;
        }
        if (position >= size) {
            break;
        }

        token_start = position;
        while ((position < size) && (!ota_ymodem_is_space(data[position]))) {
            position++;
        }
        token_end = position;
        length = token_end - token_start;

        if ((length >= 5u) && (data[token_start] == (uint8_t)'s') &&
            (data[token_start + 1u] == (uint8_t)'i') &&
            (data[token_start + 2u] == (uint8_t)'g') &&
            (data[token_start + 3u] == (uint8_t)'=')) {
            if (length == (4u + OTA_YMODEM_SIGNATURE_HEX_LEN)) {
                valid = true;
                for (index = 0u; index < OTA_IMAGE_SIGNATURE_SIZE; index++) {
                    if ((!ota_ymodem_hex_digit(data[token_start + 4u + (index * 2u)], &high)) ||
                        (!ota_ymodem_hex_digit(data[token_start + 5u + (index * 2u)], &low))) {
                        valid = false;
                        break;
                    }
                    info->signature[index] = (uint8_t)((high << 4u) | low);
                }
                if (valid) {
                    info->has_signature = true;
                    info->sig_algorithm = OTA_IMAGE_SIG_ED25519;
                }
            }
            continue;
        }

        if ((length >= 5u) && (length <= 6u) &&
            (data[token_start] == (uint8_t)'k') &&
            (data[token_start + 1u] == (uint8_t)'i') &&
            (data[token_start + 2u] == (uint8_t)'d') &&
            (data[token_start + 3u] == (uint8_t)'=')) {
            value = 0u;
            digits = 0u;
            valid = true;
            for (index = token_start + 4u; index < token_end; index++) {
                if ((!ota_ymodem_hex_digit(data[index], &digit)) || (digits >= 2u)) {
                    valid = false;
                    break;
                }
                value = (value << 4u) | digit;
                digits++;
            }
            if (valid && (digits > 0u)) {
                info->pubkey_id = (uint8_t)value;
            }
        }
    }
}

void ota_ymodem_rx_init(ota_ymodem_rx_t *rx)
{
    ota_ymodem_rx_reset_frame(rx);
    rx->index = 0u;
    rx->can_count = 0u;
    rx->packet.index = 0u;
    rx->packet.size = 0u;
}

ota_ymodem_event_t ota_ymodem_rx_feed(ota_ymodem_rx_t *rx, uint8_t byte)
{
    switch (rx->state) {
        case OTA_YMODEM_RX_HEADER:
            if (byte == OTA_YMODEM_SOH) {
                rx->payload_size = OTA_YMODEM_PACKET_SHORT;
                rx->can_count = 0u;
                rx->state = OTA_YMODEM_RX_INDEX;
            } else if (byte == OTA_YMODEM_STX) {
                rx->payload_size = OTA_YMODEM_PACKET_LONG;
                rx->can_count = 0u;
                rx->state = OTA_YMODEM_RX_INDEX;
            } else if (byte == OTA_YMODEM_EOT) {
                rx->can_count = 0u;
                ota_ymodem_rx_reset_frame(rx);
                return OTA_YMODEM_EVENT_EOT;
            } else if (byte == OTA_YMODEM_CAN) {
                rx->can_count++;
                if (rx->can_count >= OTA_YMODEM_CAN_THRESHOLD) {
                    rx->can_count = 0u;
                    ota_ymodem_rx_reset_frame(rx);
                    return OTA_YMODEM_EVENT_CAN;
                }
                ota_ymodem_rx_reset_frame(rx);
            } else {
                /* 帧间噪声（对端回显的命令行、握手期的 'C' 等）一律丢弃。 */
                rx->can_count = 0u;
            }
            break;

        case OTA_YMODEM_RX_INDEX:
            rx->index = byte;
            rx->state = OTA_YMODEM_RX_INDEX_INV;
            break;

        case OTA_YMODEM_RX_INDEX_INV:
            if ((uint8_t)(rx->index + byte) != OTA_YMODEM_INDEX_SUM) {
                /* 序号与反码不符，说明帧首是数据里的巧合字节，立刻放弃本帧。 */
                ota_ymodem_rx_reset_frame(rx);
                return OTA_YMODEM_EVENT_ERROR;
            }
            rx->position = 0u;
            rx->crc_calc = 0u;
            rx->state = OTA_YMODEM_RX_PAYLOAD;
            break;

        case OTA_YMODEM_RX_PAYLOAD:
            rx->packet.data[rx->position] = byte;
            rx->position++;
            if (rx->position >= rx->payload_size) {
                rx->crc_calc = ota_ymodem_crc16_update(rx->crc_calc,
                                                       rx->packet.data,
                                                       (uint32_t)rx->payload_size);
                rx->state = OTA_YMODEM_RX_CRC_HIGH;
            }
            break;

        case OTA_YMODEM_RX_CRC_HIGH:
            rx->crc_received = (uint16_t)((uint16_t)byte << 8u);
            rx->state = OTA_YMODEM_RX_CRC_LOW;
            break;

        case OTA_YMODEM_RX_CRC_LOW:
            rx->crc_received |= (uint16_t)byte;
            rx->state = OTA_YMODEM_RX_HEADER;
            if (rx->crc_received != rx->crc_calc) {
                return OTA_YMODEM_EVENT_ERROR;
            }
            rx->packet.index = rx->index;
            rx->packet.size = rx->payload_size;
            return OTA_YMODEM_EVENT_PACKET;

        default:
            ota_ymodem_rx_reset_frame(rx);
            break;
    }

    return OTA_YMODEM_EVENT_NONE;
}

bool ota_ymodem_parse_file_info(const uint8_t *data,
                                uint32_t size,
                                ota_ymodem_file_info_t *info)
{
    uint32_t name_length = 0u;
    uint32_t copy_length = 0u;
    uint32_t position = 0u;
    uint32_t field_end = 0u;
    uint32_t value = 0u;
    uint32_t digits = 0u;

    if ((data == NULL) || (info == NULL) || (size == 0u)) {
        return false;
    }

    info->file_size = 0u;
    info->has_version = false;
    info->version = 0u;
    info->has_signature = false;
    info->pubkey_id = 0u;
    info->sig_algorithm = OTA_IMAGE_SIG_NONE;
    (void)memset(info->file_name, 0, sizeof(info->file_name));
    (void)memset(info->signature, 0, sizeof(info->signature));

    /* 第一字段：文件名，以 '\0' 结束。 */
    while ((name_length < size) && (data[name_length] != 0u)) {
        name_length++;
    }
    if (name_length >= size) {
        return false;
    }
    if (name_length > 0u) {
        copy_length = name_length;
        if (copy_length > (uint32_t)(OTA_YMODEM_FILE_NAME_MAX - 1u)) {
            copy_length = (uint32_t)(OTA_YMODEM_FILE_NAME_MAX - 1u);
        }
        (void)memcpy(info->file_name, data, copy_length);
        info->file_name[copy_length] = '\0';
    }

    /* 第二字段：长度 [修改时间] [0x版本号]，同样以 '\0' 结束。 */
    position = name_length + 1u;
    if (position >= size) {
        return false;
    }

    field_end = position;
    while ((field_end < size) && (data[field_end] != 0u)) {
        field_end++;
    }
    if (field_end == position) {
        return false;
    }

    /* 结束包（第 0 包的空文件声明）长度为 "0"，同样要走完这一套解析。 */
    while ((position < field_end) && ota_ymodem_is_space(data[position])) {
        position++;
    }
    while ((position < field_end) &&
           (data[position] >= (uint8_t)'0') && (data[position] <= (uint8_t)'9')) {
        if (value > 0x0FFFFFFFu) {
            return false;
        }
        value = (value * 10u) + (uint32_t)(data[position] - (uint8_t)'0');
        digits++;
        position++;
    }
    if (digits == 0u) {
        return false;
    }
    info->file_size = value;

    ota_ymodem_scan_version(data + position, field_end - position, info);
    ota_ymodem_scan_signature(data + position, field_end - position, info);

    return true;
}

uint16_t ota_ymodem_crc16_update(uint16_t crc, const uint8_t *data, uint32_t size)
{
    uint32_t index;
    uint32_t bit;

    for (index = 0u; index < size; index++) {
        crc ^= (uint16_t)((uint16_t)data[index] << 8u);
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((uint16_t)(crc << 1u) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1u);
            }
        }
    }

    return crc;
}
