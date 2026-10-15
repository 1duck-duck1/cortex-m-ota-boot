/**
 ******************************************************************************
 * @file    boot_nvs.h
 * @brief   处理参数数据区里的升级请求：复核、补头、提交 pending
 *
 * App 收完固件后只能把"声明"写进 NVS，不能碰 Metadata。Boot 下次启动读到这条
 * 声明时，声明本身不构成任何信任：长度、CRC32、版本都可能是半截写入留下的，
 * 也可能指向一个根本没写成功的槽。因此这里做的是完整复核——
 * 重算整槽载荷 CRC32、预检向量表、核对镜像头，全部通过才提交 pending，
 * 最后才清掉请求。任何一步失败都保持请求原样，让下一次启动可以重来。
 ******************************************************************************
 */
#ifndef BOOT_NVS_H
#define BOOT_NVS_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_metadata.h"
#include "ota_request.h"

/** 一次请求处理的结果 */
typedef enum
{
    BOOT_NVS_RESULT_IDLE = 0,       /**< 参数区是空的，没有请求 */
    BOOT_NVS_RESULT_INVALID,        /**< 有内容但不是一条自洽的请求 */
    BOOT_NVS_RESULT_STALE,          /**< 请求指向不该被覆盖的槽，已丢弃 */
    BOOT_NVS_RESULT_RANGE,          /**< 声明的长度越出槽容量 */
    BOOT_NVS_RESULT_CRC_MISMATCH,   /**< 槽内载荷 CRC32 与声明不符 */
    BOOT_NVS_RESULT_VECTOR_INVALID, /**< 向量表首两项非法，镜像跑不起来 */
    BOOT_NVS_RESULT_SIGNATURE_INVALID, /**< 签名缺失或未通过内建公钥验证 */
    BOOT_NVS_RESULT_HEADER_FAILED,  /**< 镜像头缺失且补写失败 */
    BOOT_NVS_RESULT_COMMIT_FAILED,  /**< Metadata 落盘失败 */
    BOOT_NVS_RESULT_APPLIED         /**< 已提交 pending，可以重启进入试运行 */
} boot_nvs_result_t;

/** 一次请求处理的完整结果，供 info 命令展示 */
typedef struct
{
    boot_nvs_result_t result;   /**< 处理结论 */
    uint32_t version;           /**< 请求声明的版本号，无请求时为 0 */
    uint32_t target_slot;       /**< 请求声明的目标槽，无请求时为 BOOT_SLOT_ID_NONE */
    uint32_t image_size;        /**< 请求声明的载荷长度，单位字节 */
    uint32_t image_crc32;       /**< 请求声明的载荷 CRC32 */
} boot_nvs_report_t;

/**
 * @brief  读出参数数据区里的升级请求
 * @param  request 读出结果，不可为 NULL
 * @return true 表示存在一条结构自洽的请求
 * @retval false 参数区为空，或内容不自洽
 * @note   只做结构校验（魔术、版本、长度、记录 CRC32），不重算槽内 CRC32。
 */
bool boot_nvs_read(ota_request_t *request);

/**
 * @brief  清掉升级请求
 * @note   把记录头 4 字节写成 0：Flash 只能把 1 写成 0，这样无需再擦一次扇区
 *         就能让魔术失效。代价是同一扇区里的其他数据仍可读，所以本函数只用于
 *         "请求已处理完"这一种语义。
 */
void boot_nvs_clear(void);

/**
 * @brief  完整处理一次升级请求
 * @param  running_slot 本次启动选中的槽，BOOT_SLOT_ID_NONE 表示停在升级模式
 * @param  metadata     当前 Metadata，成功后在其中标记 pending
 * @return 处理结果
 * @note   本函数只改内存里的 metadata，落盘由本函数内部完成，调用方拿到
 *         APPLIED 后需要自行重启，让启动决策走一遍 pending 试运行。
 */
boot_nvs_report_t boot_nvs_process(uint32_t running_slot, boot_metadata_t *metadata);

/**
 * @brief  取处理结论的可读名称
 * @param  result 处理结论
 * @return 静态字符串，永不返回 NULL
 */
const char *boot_nvs_result_name(boot_nvs_result_t result);

#endif /* BOOT_NVS_H */
