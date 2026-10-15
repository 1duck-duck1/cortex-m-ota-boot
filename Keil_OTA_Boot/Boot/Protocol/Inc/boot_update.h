/**
 ******************************************************************************
 * @file    boot_update.h
 * @brief   Boot 侧 OTA 接收状态机：握手 → 收包 → 收尾
 *
 * 三段式状态机的核心取舍是"收包期间绝不回过头去动 Flash 的元数据"：
 * 握手期只擦目标槽，收包期只顺序追加并累计 CRC32，收尾期才回读复核、
 * 补镜像头、提交 pending。这样任何一次掉电最多毁掉正在收的那个槽，
 * Metadata 与另一个槽始终完好，设备永远有路可退。
 *
 * 收到的数据一律当作"裸载荷"处理：它从槽首 + 0x200 处开始，镜像头由 Boot
 * 在收尾时补写。这样上位机只需要发一个 .bin，不必理解槽的布局。
 *
 * 线上约定（与配套上位机一致）：第 0 包带文件长度、可选版本号与可选签名
 * （sig=<128 hex> kid=<2 hex>，走 STX/1024 帧）；Boot 校验通过后先擦槽再回
 * ACK + 'C'；数据包逐包 ACK；EOT 单次即回 ACK 并进入收尾，收尾结果通过命令行
 * 应答给出，不再走 YMODEM 的第二轮空包握手。
 ******************************************************************************
 */
#ifndef BOOT_UPDATE_H
#define BOOT_UPDATE_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_metadata.h"
#include "boot_slot_writer.h"
#include "ota_ymodem.h"

/** 会话所处阶段 */
typedef enum
{
    BOOT_UPDATE_STATE_IDLE = 0,     /**< 没有会话 */
    BOOT_UPDATE_STATE_HANDSHAKE,    /**< 周期发 'C'，等第 0 包 */
    BOOT_UPDATE_STATE_RECEIVE,      /**< 逐包接收、写入并累计 CRC32 */
    BOOT_UPDATE_STATE_FINAL,        /**< 回读复核与提交 */
    BOOT_UPDATE_STATE_DONE,         /**< 已提交 pending，等待重启 */
    BOOT_UPDATE_STATE_FAILED        /**< 已终止，原因见 reason */
} boot_update_state_t;

/** 会话终止原因 */
typedef enum
{
    BOOT_UPDATE_REASON_NONE = 0,        /**< 尚未终止 */
    BOOT_UPDATE_REASON_TIMEOUT,         /**< 静默超时，对端没动静 */
    BOOT_UPDATE_REASON_STALLED,         /**< 一直有字节但迟迟不成帧 */
    BOOT_UPDATE_REASON_CANCELLED,       /**< 对端主动中止，或本端按了 Ctrl-C */
    BOOT_UPDATE_REASON_RETRY_EXCEEDED,  /**< 同一包重传次数超限 */
    BOOT_UPDATE_REASON_NO_VERSION,      /**< 命令行与第 0 包都没给出有效版本号 */
    BOOT_UPDATE_REASON_BAD_SLOT,        /**< 目标槽不可用（非法或受保护） */
    BOOT_UPDATE_REASON_BAD_SIZE,        /**< 第 0 包声明的长度非法 */
    BOOT_UPDATE_REASON_ERASE_FAILED,    /**< 目标槽擦除失败 */
    BOOT_UPDATE_REASON_WRITE_FAILED,    /**< 写入或补镜像头失败 */
    BOOT_UPDATE_REASON_VERIFY_FAILED,   /**< 回读 CRC32 不符或向量表非法 */
    BOOT_UPDATE_REASON_SIGNATURE_FAILED,/**< 镜像未带签名，或签名未通过验证 */
    BOOT_UPDATE_REASON_COMMIT_FAILED    /**< Metadata 落盘失败 */
} boot_update_reason_t;

/** 一次会话的完整状态，由调用方持有；含 YMODEM 组帧缓冲，体积较大，不要放栈上 */
typedef struct
{
    boot_metadata_t *metadata;      /**< 元数据，收尾成功后在其中标记 pending */
    ota_ymodem_rx_t rx;             /**< YMODEM 组帧状态机 */
    boot_slot_writer_t writer;      /**< 目标槽写入会话 */
    boot_update_state_t state;      /**< 当前阶段 */
    boot_update_reason_t reason;    /**< 终止原因 */
    uint32_t target_slot;           /**< 目标槽编号 */
    uint32_t protected_slot;        /**< 受保护槽，不允许被覆盖 */
    uint32_t version;               /**< 固件版本号 */
    bool has_version;               /**< 命令行是否已给出版本号 */
    uint32_t expected_index;        /**< 下一个期待的包序号，取低 8 位比较 */
    uint32_t retry_count;           /**< 当前包连续出错次数 */
    uint32_t image_size;            /**< 最终载荷长度，单位字节 */
    uint32_t image_crc32;           /**< 最终载荷 CRC32 */
    bool has_signature;             /**< 第 0 包是否带完整 sig= 字段 */
    uint8_t sig_algorithm;          /**< 签名算法标识，has_signature 时取自 OTA_IMAGE_SIG_* */
    uint8_t pubkey_id;              /**< 签名所用公钥编号 */
    uint8_t signature[OTA_IMAGE_SIGNATURE_SIZE];    /**< 第 0 包携带的签名 */
    char file_name[OTA_YMODEM_FILE_NAME_MAX];   /**< 第 0 包声明的文件名，仅供日志 */
    uint32_t c_tick;                /**< 上次发出 'C' 的时刻 */
    uint32_t progress_tick;         /**< 最后一个字节到达的时刻 */
    uint32_t state_tick;            /**< 进入当前阶段的时刻 */
} boot_update_t;

/**
 * @brief  初始化会话对象
 * @param  update   待初始化的会话，不可为 NULL
 * @param  metadata 当前 Metadata，不可为 NULL
 * @note   只建立关联，不启动会话；升级模式整个生命周期只需要初始化一次。
 */
void boot_update_init(boot_update_t *update, boot_metadata_t *metadata);

/**
 * @brief  启动一次接收会话
 * @param  update         会话对象，不可为 NULL
 * @param  target_slot    目标槽，由调用方按"非运行槽 / 非受保护槽"推出
 * @param  protected_slot 受保护槽，传 BOOT_SLOT_ID_NONE 表示无保护对象
 * @param  version        命令行给出的版本号
 * @param  has_version    命令行是否给出了版本号
 * @return true 表示会话已启动
 * @retval false 已有会话在进行，或目标槽非法
 * @note   返回 true 只是"登记成功"，此时还没有任何数据到达；真正能不能收成，
 *         要等会话结束后看状态。这与"命令立即回应"的直觉不同，是为了让
 *         上位机在命令应答之后立刻开始发送，不给链路留空档。首个 'C' 由
 *         第一次 boot_update_poll() 发出，因此调用方应当先回命令应答再轮询。
 */
bool boot_update_start(boot_update_t *update,
                       uint32_t target_slot,
                       uint32_t protected_slot,
                       uint32_t version,
                       bool has_version);

/**
 * @brief  推进会话：收字节、判超时、周期发 'C'
 * @param  update 会话对象，不可为 NULL
 * @note   会话不在进行中时立即返回。本函数会独占串口读取，调用方不要在它
 *         旁边再去读同一个串口，否则字节会被两个读者瓜分。
 */
void boot_update_poll(boot_update_t *update);

/**
 * @brief  立即终止会话
 * @param  update 会话对象，不可为 NULL
 * @note   只关闭会话，不回滚已写入的内容：槽已经半写，Metadata 从未引用过它。
 */
void boot_update_abort(boot_update_t *update);

/**
 * @brief  查询会话是否正在进行
 * @param  update 会话对象
 * @return true 表示处于握手、收包或收尾阶段
 */
bool boot_update_is_busy(const boot_update_t *update);

/**
 * @brief  取当前阶段
 * @param  update 会话对象
 * @return 当前阶段；update 为 NULL 时返回 BOOT_UPDATE_STATE_IDLE
 */
boot_update_state_t boot_update_state(const boot_update_t *update);

/**
 * @brief  取终止原因
 * @param  update 会话对象
 * @return 终止原因；未终止时为 BOOT_UPDATE_REASON_NONE
 */
boot_update_reason_t boot_update_reason(const boot_update_t *update);

/**
 * @brief  取阶段的可读名称
 * @param  state 阶段
 * @return 静态字符串，永不返回 NULL
 */
const char *boot_update_state_name(boot_update_state_t state);

/**
 * @brief  取终止原因的可读名称
 * @param  reason 终止原因
 * @return 静态字符串，永不返回 NULL
 */
const char *boot_update_reason_name(boot_update_reason_t reason);

#endif /* BOOT_UPDATE_H */
