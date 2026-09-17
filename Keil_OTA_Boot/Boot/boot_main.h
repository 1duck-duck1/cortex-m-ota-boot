/**
 ******************************************************************************
 * @file    boot_main.h
 * @brief   Bootloader 主流程
 ******************************************************************************
 */
#ifndef BOOT_MAIN_H
#define BOOT_MAIN_H

/**
 * @brief  Bootloader 主流程，正常情况下不返回
 * @note   在 main.c 的 USER CODE 2 区调用（HAL_Init 与时钟配置之后）
 */
void boot_run(void);

#endif /* BOOT_MAIN_H */
