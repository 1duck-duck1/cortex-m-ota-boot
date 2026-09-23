/**
 ******************************************************************************
 * @file    app_nvs.c
 * @brief   升级请求记录的落盘
 ******************************************************************************
 */
#include "app_nvs.h"

#include "app_flash.h"
#include "ota_request.h"

#include "stm32f4xx_hal.h"

#include <stddef.h>

/** 请求记录里"在 magic 之前"的字段数：version / target_slot / image_size / image_crc32 */
#define APP_NVS_BODY_WORDS  4u

bool app_nvs_write_request(uint32_t version,
                           uint32_t target_slot,
                           uint32_t image_size,
                           uint32_t image_crc32)
{
    ota_request_t request;

    ota_request_build(&request, version, target_slot, image_size, image_crc32);

    /* 先擦再写：Flash 只能把 1 写成 0，同一地址二次写入前必须回到全 1，
       否则新旧记录会按位相与，得到一条谁的字段都对不上的记录。 */
    if (!app_flash_erase_nvs())
    {
        return false;
    }

    /* 落盘顺序（与结构体字段顺序一致）：
       1. 先写 version..image_crc32 四个字；
       2. 再写 magic——它一旦就位，记录就"看起来有效"了；
       3. 最后写 record_crc32——它一旦就位，记录才真正完整。
       掉电落在任何一步之间，Boot 侧 ota_request_is_valid() 都会拒绝。 */
    if (!app_flash_write_nvs((uint32_t)offsetof(ota_request_t, version),
                             (const uint8_t *)&request.version,
                             APP_NVS_BODY_WORDS * (uint32_t)sizeof(uint32_t)))
    {
        return false;
    }
    if (!app_flash_write_nvs((uint32_t)offsetof(ota_request_t, magic),
                             (const uint8_t *)&request.magic,
                             (uint32_t)sizeof(uint32_t)))
    {
        return false;
    }
    if (!app_flash_write_nvs((uint32_t)offsetof(ota_request_t, record_crc32),
                             (const uint8_t *)&request.record_crc32,
                             (uint32_t)sizeof(uint32_t)))
    {
        return false;
    }

    /* Flash 编程返回时字节已落盘，这里只保证"记录写完"这一事实对后续代码
       与随后的复位可见（复位由调用方在本函数返回后触发）。 */
    __DSB();

    return true;
}
