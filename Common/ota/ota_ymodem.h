/**
 ******************************************************************************
 * @file    ota_ymodem.h
 * @brief   YMODEM 接收侧组帧与文件信息包解析（纯逻辑，无硬件依赖）
 *
 * 只实现 128/1024 字节数据包、CRC16 校验、EOT 与双 CAN 四种线上事件，够
 * Boot 与 App 共用。发送侧由上位机承担，这里不提供：设备永远是被动方。
 ******************************************************************************
 */
#ifndef OTA_YMODEM_H
#define OTA_YMODEM_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_image.h"

#define OTA_YMODEM_SOH              (0x01u)   /* 128 字节数据包的帧首 */
#define OTA_YMODEM_STX              (0x02u)   /* 1024 字节数据包的帧首 */
#define OTA_YMODEM_EOT              (0x04u)   /* 传输结束 */
#define OTA_YMODEM_ACK              (0x06u)   /* 接收正确 */
#define OTA_YMODEM_NAK              (0x15u)   /* 接收出错，请求重传 */
#define OTA_YMODEM_CAN              (0x18u)   /* 主动中止，连续两次才算数 */
#define OTA_YMODEM_C                (0x43u)   /* 请求以 CRC16 模式开始传输 */

/** 短包数据体长度，单位字节 */
#define OTA_YMODEM_PACKET_SHORT     (128u)

/** 长包数据体长度，单位字节 */
#define OTA_YMODEM_PACKET_LONG      (1024u)

/** 单包数据体的最大长度，也是接收缓冲区的长度 */
#define OTA_YMODEM_PACKET_DATA_MAX  (OTA_YMODEM_PACKET_LONG)

/** 文件名缓冲长度，超长文件名只保留前若干字节用于日志 */
#define OTA_YMODEM_FILE_NAME_MAX    (64u)

/** 连续多少个 CAN 才判定为对端主动中止 */
#define OTA_YMODEM_CAN_THRESHOLD    (2u)

/** sig= 字段的十六进制字符数，64 字节签名展开为 128 个字符 */
#define OTA_YMODEM_SIGNATURE_HEX_LEN   (OTA_IMAGE_SIGNATURE_SIZE * 2u)

/** 组帧结果，每一个非 NONE 事件都代表"有进展"，是超时计时的基准 */
typedef enum
{
    OTA_YMODEM_EVENT_NONE = 0,  /**< 还没凑齐一帧 */
    OTA_YMODEM_EVENT_PACKET,    /**< 收到一个校验正确的包 */
    OTA_YMODEM_EVENT_EOT,       /**< 收到传输结束符 */
    OTA_YMODEM_EVENT_CAN,       /**< 收到连续两个 CAN，对端中止 */
    OTA_YMODEM_EVENT_ERROR      /**< 帧内校验失败，调用方应回 NAK 请对端重传 */
} ota_ymodem_event_t;

/** 一个成帧的 YMODEM 数据包 */
typedef struct
{
    uint8_t index;                              /**< 8 位包序号，长镜像必然回绕 */
    uint16_t size;                              /**< 本包数据体长度，128 或 1024 */
    uint8_t data[OTA_YMODEM_PACKET_DATA_MAX];   /**< 数据体，不足部分由发送方以 0x1A 填充 */
} ota_ymodem_packet_t;

/** 组帧状态机的内部阶段 */
typedef enum
{
    OTA_YMODEM_RX_HEADER = 0,   /**< 找帧首，同时识别 EOT 与 CAN */
    OTA_YMODEM_RX_INDEX,        /**< 收包序号 */
    OTA_YMODEM_RX_INDEX_INV,    /**< 收包序号反码，用于即时发现错位 */
    OTA_YMODEM_RX_PAYLOAD,      /**< 收数据体 */
    OTA_YMODEM_RX_CRC_HIGH,     /**< 收 CRC16 高字节 */
    OTA_YMODEM_RX_CRC_LOW       /**< 收 CRC16 低字节 */
} ota_ymodem_rx_state_t;

/** 接收侧状态机，由调用方持有（栈上或静态皆可） */
typedef struct
{
    ota_ymodem_rx_state_t state;    /**< 当前组帧阶段 */
    ota_ymodem_packet_t packet;     /**< 最近一次成帧的结果 */
    uint16_t payload_size;          /**< 本帧声明的数据体长度 */
    uint16_t position;              /**< 数据体已收字节数 */
    uint16_t crc_calc;              /**< 本帧数据体的 CRC16 累计值 */
    uint16_t crc_received;          /**< 本帧携带的 CRC16 */
    uint8_t index;                  /**< 本帧声明的包序号 */
    uint8_t can_count;              /**< 连续 CAN 计数 */
} ota_ymodem_rx_t;

/** 第 0 包解析出的文件信息 */
typedef struct
{
    uint32_t file_size;                         /**< 文件长度，单位字节 */
    bool has_version;                           /**< 第二个字段里是否声明了版本号 */
    uint32_t version;                           /**< 版本号，has_version 为 true 时有效 */
    bool has_signature;                         /**< 第二个字段里是否带完整 sig= 字段 */
    uint8_t pubkey_id;                          /**< kid= 解析出的公钥编号 */
    uint8_t sig_algorithm;                      /**< 签名算法标识，has_signature 时取自 OTA_IMAGE_SIG_* */
    uint8_t signature[OTA_IMAGE_SIGNATURE_SIZE];/**< sig= 解析出的签名，has_signature 为 false 时全 0 */
    char file_name[OTA_YMODEM_FILE_NAME_MAX];   /**< 文件名，仅用于日志 */
} ota_ymodem_file_info_t;

/**
 * @brief  复位接收状态机
 * @param  rx 待复位的状态机
 * @note   每次会话开始、以及在回 NAK 之前都要调用：让对端从帧首重发，
 *         否则半帧的残留字节会被当成新帧的帧首。
 */
void ota_ymodem_rx_init(ota_ymodem_rx_t *rx);

/**
 * @brief  喂一个字节，推进组帧状态机
 * @param  rx   接收状态机
 * @param  byte 收到的字节
 * @return 本次喂入产生的事件，NONE 表示还在组帧中
 * @note   返回 ERROR 时状态机已自动复位，调用方回一个 NAK 即可；
 *         返回 PACKET 后数据在 rx->packet 里，下次喂入会覆盖它。
 */
ota_ymodem_event_t ota_ymodem_rx_feed(ota_ymodem_rx_t *rx, uint8_t byte);

/**
 * @brief  解析第 0 包（文件信息包）
 * @param  data 数据体指针
 * @param  size 数据体长度，单位字节
 * @param  info 解析结果
 * @return true 表示格式合法
 * @retval false 缺少文件名或长度字段，或长度为 0
 * @note   第二字段形如 "<长度> [修改时间] [0x版本号] [sig=<128 hex>] [kid=<2 hex>]"。
 *         版本号必须带 0x 前缀才被认作版本，否则与标准 mtime 无法区分，一律忽略；
 *         sig= 必须是恰好 OTA_YMODEM_SIGNATURE_HEX_LEN 个十六进制字符，kid= 为 1~2
 *         位十六进制。任何一项格式非法都只忽略该项，不影响长度与文件名的解析。
 */
bool ota_ymodem_parse_file_info(const uint8_t *data,
                                uint32_t size,
                                ota_ymodem_file_info_t *info);

/**
 * @brief  增量计算 YMODEM 的 CRC16（poly 0x1021，初值 0）
 * @param  crc  上一段的中间值，首段传 0
 * @param  data 数据指针
 * @param  size 字节数
 * @return 新的中间值
 * @note   只覆盖数据体，不含帧首与包序号——这是 XMODEM-CRC 的口径，
 *         把序号也算进去会导致与所有标准上位机都对不上校验。
 */
uint16_t ota_ymodem_crc16_update(uint16_t crc, const uint8_t *data, uint32_t size);

#endif /* OTA_YMODEM_H */
