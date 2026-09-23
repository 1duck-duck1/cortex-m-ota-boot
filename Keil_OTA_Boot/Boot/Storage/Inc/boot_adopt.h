/**
 ******************************************************************************
 * @file    boot_adopt.h
 * @brief   给"Keil 直接烧录、没有长度信息"的槽补一份镜像头
 *
 * 这是开发辅助通道，不是量产路径。日常改 App 时用 Keil 的 F8 把 App 烧进某个
 * 槽，槽里没有镜像头，也没有 NVS 请求，Boot 自然不认它。本模块从槽内容反推
 * 边界：向量表提供合法入口，槽尾最后一个非 0xFF 字节提供长度，再重算载荷
 * CRC32，凑齐这三样就能补出一份自洽的镜像头并提交 pending。
 *
 * 提交的是 pending 而不是直接置 active：让 Keil 烧进去的镜像也走一遍
 * "试运行 + App 确认"，正好顺便验证确认与回滚链路。
 *
 * 两条限制不能放松：只允许在头区为擦除态时补写（已写过头必须整槽重传），
 * 且不允许覆盖受保护槽（正在跑的槽，或仍能启动的 active 槽）。
 ******************************************************************************
 */
#ifndef BOOT_ADOPT_H
#define BOOT_ADOPT_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_metadata.h"

/** 一次 adopt 的结果 */
typedef enum
{
    BOOT_ADOPT_OK = 0,          /**< 已补头并提交 pending */
    BOOT_ADOPT_ERR_SLOT,        /**< 槽编号非法 */
    BOOT_ADOPT_ERR_PROTECTED,   /**< 目标槽是受保护槽 */
    BOOT_ADOPT_ERR_VERSION,     /**< 版本号缺失或非法 */
    BOOT_ADOPT_ERR_HEADER,      /**< 头区已被写过，必须整槽重传 */
    BOOT_ADOPT_ERR_NO_IMAGE,    /**< 槽里没有可识别的镜像 */
    BOOT_ADOPT_ERR_FAILED       /**< 补头或 Metadata 落盘失败 */
} boot_adopt_result_t;

/** 一次 adopt 的完整结果，供命令行输出 */
typedef struct
{
    boot_adopt_result_t result; /**< 处理结论 */
    uint32_t slot_id;           /**< 目标槽 */
    uint32_t image_size;        /**< 反推出的载荷长度，单位字节 */
    uint32_t image_crc32;       /**< 重算出的载荷 CRC32 */
} boot_adopt_report_t;

/**
 * @brief  给一个槽补镜像头并提交 pending
 * @param  slot_id        目标槽，BOOT_SLOT_ID_A / BOOT_SLOT_ID_B
 * @param  protected_slot 受保护槽，传 BOOT_SLOT_ID_NONE 表示无保护对象
 * @param  version        固件版本号，不允许为 OTA_REQUEST_VERSION_NONE
 * @param  metadata       当前 Metadata，成功后在其中标记 pending
 * @param  report         处理结果，允许为 NULL
 * @return 处理结论
 * @note   调用方必须保证当前处于升级模式：本函数只看几何与内容，不看启动状态，
 *         在正常运行路径上调用它会把一份未经传输校验的镜像扶正。
 */
boot_adopt_result_t boot_adopt_slot(uint32_t slot_id,
                                    uint32_t protected_slot,
                                    uint32_t version,
                                    boot_metadata_t *metadata,
                                    boot_adopt_report_t *report);

/**
 * @brief  取处理结论的可读名称
 * @param  result 处理结论
 * @return 静态字符串，永不返回 NULL
 */
const char *boot_adopt_result_name(boot_adopt_result_t result);

#endif /* BOOT_ADOPT_H */
