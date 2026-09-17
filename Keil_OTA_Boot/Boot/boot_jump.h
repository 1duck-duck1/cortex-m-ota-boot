/**
 ******************************************************************************
 * @file    boot_jump.h
 * @brief   应用有效性校验与跳转
 ******************************************************************************
 */
#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  校验槽内固件向量表合法后跳转（F-01 简化版 + F-02）
 * @param  slot_base 槽起始地址（如 BOOT_SLOT_A_ADDR）
 * @param  slot_size 槽大小（字节）
 * @retval false：校验失败（空槽 / SP 或 PC 越界）
 * @retval true ：不会发生——跳转成功后控制权已交给 App
 */
bool boot_jump_slot(uint32_t slot_base, uint32_t slot_size);

#endif /* BOOT_JUMP_H */
