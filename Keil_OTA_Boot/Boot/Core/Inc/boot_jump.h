/**
 ******************************************************************************
 * @file    boot_jump.h
 * @brief   从槽首取向量并跳转过去
 *
 * Direct-XIP 下 Boot 不搬运镜像，只做一件事：把 MSP 与 PC 换成目标槽的，
 * 然后让 CPU 从这里继续执行。跳转前必须把 Boot 自己的痕迹清理干净——
 * 中断、SysTick、VTOR 都属于上一个世界，留着任何一样都会让 App 在第一次
 * 中断时跳回 Boot 的中断向量里去。
 *
 * 向量表落在"槽首 + 0x200"而不是槽首：槽首那 0x200 字节是镜像头，同时充当
 * VTOR 的对齐缓冲（SCB_VTOR_TBLOFF_Msk 会截断低 7 位）。
 ******************************************************************************
 */
#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdbool.h>
#include <stdint.h>

/** 一个可跳转的镜像入口：新向量表地址、初始 MSP 与复位入口 */
typedef struct
{
    uint32_t vtor;  /**< 新向量表地址，即槽首 + OTA_IMAGE_LOAD_OFFSET */
    uint32_t sp;    /**< 初始主栈指针，取自向量表首字 */
    uint32_t pc;    /**< 复位入口，取自向量表次字，最低位为 1 */
} boot_jump_vector_t;

/**
 * @brief  从目标槽读出并校验跳转向量
 * @param  slot_id    BOOT_SLOT_ID_A / BOOT_SLOT_ID_B
 * @param  image_size 该槽载荷长度，单位字节，用于判定入口是否落在槽内
 * @param  vector     读出结果，不可为 NULL
 * @return true 表示 SP / PC 均合法
 * @note   这是跳转前的最后一道闸门，与 boot_image_check() 的第三关同源；
 *         启动决策已经查过一次，这里再查是为了让跳转本身不依赖调用方的正确性。
 */
bool boot_jump_vector(uint32_t slot_id, uint32_t image_size, boot_jump_vector_t *vector);

/**
 * @brief  跳转到目标镜像
 * @param  vector 已校验的跳转向量，不可为 NULL
 * @note   本函数永不返回：切换 MSP 之后 Boot 的栈就不再有效了。
 *         调用前必须已经关闭串口（boot_transport_deinit()），否则残留的
 *         外设时钟与引脚状态会带进 App。
 */
void boot_jump_to(const boot_jump_vector_t *vector);

/**
 * @brief  软复位
 * @note   本函数永不返回。用软复位而不是直接跳转，是为了让下一次启动重新走
 *         一遍完整的决策流程（升级收尾后必须经过决策才能选中新槽）。
 */
void boot_jump_restart(void);

#endif /* BOOT_JUMP_H */
