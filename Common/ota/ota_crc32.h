/**
 ******************************************************************************
 * @file    ota_crc32.h
 * @brief   CRC32 增量计算（poly 0xEDB88320，init / xorout 均为 0xFFFFFFFF）
 *
 * Boot 与 App 必须编译同一份实现，否则"边收边算"的校验值无法互认，也与上位机
 * 的 zlib.crc32 对不上。本模块不依赖任何硬件，被两个工程同源编译。
 ******************************************************************************
 */
#ifndef OTA_CRC32_H
#define OTA_CRC32_H

#include <stdint.h>

#define OTA_CRC32_INIT_VALUE      (0xFFFFFFFFu)   /* 中间值初值，等价于 zlib.crc32 的 0 */
#define OTA_CRC32_XOROUT_VALUE    (0xFFFFFFFFu)   /* 输出异或值，与 init 一起凑齐 zlib 口径 */

/**
 * @brief  增量计算一段数据的 CRC32
 * @param  crc   上一段返回的中间值；第一段必须传 OTA_CRC32_INIT_VALUE
 * @param  data  数据指针，size 为 0 时允许为 NULL
 * @param  size  字节数
 * @return 新的中间值，必须再经 ota_crc32_final() 异或才是最终结果
 * @note   "边收边算"靠的就是这个函数：每收一段推进一次，收完再做最后异或。
 *         中间值不能直接拿去和上位机的 CRC32 比较。
 */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size);

/**
 * @brief  把中间值转换成最终 CRC32
 * @param  crc ota_crc32_update() 返回的中间值
 * @return 最终 CRC32，与 zlib.crc32 结果一致
 */
uint32_t ota_crc32_final(uint32_t crc);

/**
 * @brief  一次性计算一段数据的 CRC32
 * @param  data 数据指针
 * @param  size 字节数
 * @return 最终 CRC32
 * @note   用于整槽回读校验这类"一次性算完"的场景。
 */
uint32_t ota_crc32_calc(const uint8_t *data, uint32_t size);

#endif /* OTA_CRC32_H */
