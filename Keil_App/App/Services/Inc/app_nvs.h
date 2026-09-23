/**
 ******************************************************************************
 * @file    app_nvs.h
 * @brief   App 侧向参数数据区（NVS）递交升级请求
 *
 * App 收完包后不能自己写 Metadata（那是 Boot 的职责，见 AGENTS.md §10），只能
 * 把一份"声明"落盘到 NVS：目标槽、长度、CRC32 与版本。Boot 下次启动读到请求后
 * 必须对目标槽重算全槽 CRC32 并与声明比对，通过才提交 Pending。
 ******************************************************************************
 */
#ifndef APP_NVS_H
#define APP_NVS_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  擦除 NVS 并写入一条升级请求记录
 * @param  version      固件版本，来自命令行或 YMODEM 第 0 包
 * @param  target_slot  APP_SLOT_ID_A / APP_SLOT_ID_B
 * @param  image_size   实收镜像字节数
 * @param  image_crc32  实收数据累计 CRC32
 * @return true 表示记录已完整落盘
 * @retval false 擦除或编程失败，此时请求不完整，Boot 侧会拒绝它
 * @note   擦除会清掉 NVS 其余内容（本轮除请求外没有别的参数）。
 *         落盘顺序是"先字段、再 magic、最后 record_crc32"，任何时刻掉电留下的
 *         都是半截记录，Boot 侧结构校验必然拒绝，不会拿半截数据去重算 CRC。
 */
bool app_nvs_write_request(uint32_t version,
                           uint32_t target_slot,
                           uint32_t image_size,
                           uint32_t image_crc32);

#endif /* APP_NVS_H */
