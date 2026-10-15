/**
 ******************************************************************************
 * @file    app_ota_service.h
 * @brief   App 侧升级编排：YMODEM 握手 → 收包写非活动槽 → 递交 NVS 请求
 *
 * 流程与 Boot 侧 boot_update 逐段对齐（见 docs/04-boot-implementation.md §8.2），
 * 只有"最后一步"不同：
 *   HANDSHAKE 收第 0 包 → 擦除非活动槽 → ACK + 'C'
 *   RECEIVE   逐包校验后写槽，同时累计 CRC32
 *   FINAL     收结束包 → 回读槽内数据重算 CRC32 → 预检向量表 → 写 NVS 请求
 * Boot 在 FINAL 直接 `mark_pending` 并软复位；App 没有写 Metadata 的权限，只能
 * 落一份声明到 NVS，再由调用方复位回 Boot 去消费（AGENTS.md §10）。
 *
 * 每一步都非阻塞：每轮只取走 DMA 环形缓冲里已到达的字节，绝不等待对端，
 * 否则收包期间的超时与 CAN 中止都无法及时响应。
 ******************************************************************************
 */
#ifndef APP_OTA_SERVICE_H
#define APP_OTA_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/** 升级会话的结束状态 */
typedef enum
{
    APP_OTA_BUSY = 0,       /**< 会话进行中，或尚未启动 */
    APP_OTA_DONE,           /**< 收齐、落盘校验通过、NVS 请求已递交 */
    APP_OTA_ABORTED,        /**< CAN / 超时 / 重传超限 / 设备侧 Flash 出错 */
    APP_OTA_REJECTED        /**< 数据不符：版本缺失、长度非法、序号错乱等 */
} app_ota_result_t;

/** 会话中止或拒绝的具体原因，用于输出可读的失败响应 */
typedef enum
{
    APP_OTA_REASON_NONE = 0,            /**< 成功，无原因 */
    APP_OTA_REASON_CANCELLED,           /**< 对端发送 CAN 主动中止 */
    APP_OTA_REASON_TIMEOUT,             /**< 对端静默超时 */
    APP_OTA_REASON_STALLED,             /**< 一直在收字节但长时间没有成帧事件 */
    APP_OTA_REASON_RETRY_EXCEEDED,      /**< 连续重传次数超过上限 */
    APP_OTA_REASON_FILE_INFO_INVALID,   /**< 第 0 包不是合法的文件信息包 */
    APP_OTA_REASON_VERSION_MISSING,     /**< 命令行与第 0 包都没有版本声明 */
    APP_OTA_REASON_SIZE_INVALID,        /**< 声明的镜像长度为空或超过槽容量 */
    APP_OTA_REASON_SIZE_MISMATCH,       /**< 实收长度与声明长度不符 */
    APP_OTA_REASON_SEQUENCE_ERROR,      /**< 包序号或阶段顺序错乱 */
    APP_OTA_REASON_SLOT_INVALID,        /**< 目标槽不可写：是运行槽或长度越界 */
    APP_OTA_REASON_FLASH_FAILED,        /**< 槽内擦除、写入或回读失败 */
    APP_OTA_REASON_CRC_MISMATCH,        /**< 回读重算的 CRC32 与收包 CRC32 不符 */
    APP_OTA_REASON_IMAGE_INVALID,       /**< 槽首向量表的 SP/PC 超出合法范围 */
    APP_OTA_REASON_NVS_FAILED           /**< NVS 擦除或请求记录写入失败 */
} app_ota_reason_t;

/** 会话的只读结果快照，供命令行输出与调试观察 */
typedef struct
{
    uint32_t target_slot;       /**< 本次会话的目标槽（APP_SLOT_ID_A/B） */
    uint32_t version;           /**< 本次会话采用的版本号 */
    uint32_t size;              /**< 声明 / 实收的镜像长度 */
    uint32_t crc32;             /**< 实收数据累计 CRC32 */
    app_ota_result_t result;    /**< 最近一次会话的结束状态 */
    app_ota_reason_t reason;    /**< 最近一次会话的原因 */
    bool has_signature;         /**< 第 0 包是否带 sign= 签名声明 */
    uint8_t pubkey_id;          /**< 签名所用公钥编号，无签名时为 0 */
} app_ota_status_t;

/**
 * @brief  登记一次升级请求（由命令行 `ota recv [version]` 调用）
 * @param  version      命令行给出的版本号
 * @param  has_version  false 表示命令行未给出，运行时回退到第 0 包声明
 * @note   只登记不启动：本函数在命令行解析过程中被调用，此时串口还握在命令行
 *         手里，立刻开跑会与同一批字节的后续解析抢数据。真正的启动发生在
 *         命令行轮询返回之后的 app_ota_service_run()。
 */
void app_ota_service_request(uint32_t version, bool has_version);

/** @brief 升级会话是否进行中（会话期间串口由本模块独占） */
bool app_ota_service_is_active(void);

/**
 * @brief  泵一次升级子状态机
 * @return APP_OTA_BUSY 表示进行中或未启动；其他值表示会话刚刚结束
 * @note   必须在主循环里反复调用；没有待处理请求时立即返回。
 *         返回 APP_OTA_DONE 时 NVS 请求已落盘，调用方应在响应发完之后做一次
 *         **干净**的软复位（NVIC_SystemReset）。不要用
 *         app_boot_client_request_boot()：那会把邮箱的 REQB 置位，Boot 因此停在
 *         升级模式，而不是去启动刚收到的 pending 镜像。
 */
app_ota_result_t app_ota_service_run(void);

/** @brief 最近一次会话的结果快照，不会为 NULL */
const app_ota_status_t *app_ota_service_status(void);

/**
 * @brief  取原因的可读名称
 * @param  reason 原因
 * @return 静态字符串，命令响应与调试日志共用
 */
const char *app_ota_reason_name(app_ota_reason_t reason);

/**
 * @brief  槽内是否存在向量表合法的镜像
 * @param  slot_id APP_SLOT_ID_A / APP_SLOT_ID_B
 * @return true 表示槽首的初始 MSP 落在 SRAM 内、入口地址落在本槽内且为 Thumb 地址
 * @note   与 FINAL 阶段提交前的预检同一套判据（docs/04-boot-implementation.md
 *         §8.2 的四条规则），`slot` 命令用它报告两槽的粗略状态。App 读不到
 *         Metadata，因此只能判断"有没有看起来可启动的镜像"，报不出长度与 CRC。
 */
bool app_ota_service_slot_has_image(uint32_t slot_id);

#endif /* APP_OTA_SERVICE_H */
