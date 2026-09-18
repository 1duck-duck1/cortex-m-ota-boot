/**
 ******************************************************************************
 * @file    boot_jump.c
 * @brief   应用有效性校验、Bootloader 环境剥离、跳转
 ******************************************************************************
 */
#include "boot_jump.h"
#include "boot_conf.h"
#include "stm32f4xx_hal.h"

/**
 * @brief  向量表合法性检查（F-01 简化版：无固件头，只查 SP/PC 范围）
 */
static bool vector_table_valid(uint32_t slot_base, uint32_t slot_size)
{
    uint32_t sp;
    uint32_t pc;

    if ((slot_size < 8u) ||
        (slot_base < BOOT_SLOT_A_ADDR) ||
        (slot_base > (BOOT_RESERVED_ADDR - slot_size)))
    {
        return false;
    }

    sp = *(volatile uint32_t *)slot_base;
    pc = *(volatile uint32_t *)(slot_base + 4u);

    if (sp == 0xFFFFFFFFu)                              /* 槽为空（擦除态） */
    {
        return false;
    }
    if (sp < BOOT_RAM_BASE || sp > BOOT_RAM_END)        /* 初始 MSP 必须在 SRAM */
    {
        return false;
    }
    if (pc < slot_base || pc >= slot_base + slot_size)  /* 入口必须在本槽内 */
    {
        return false;
    }
    if ((pc & 1u) == 0u)
    {
        return false;
    }
    return true;
}

bool boot_image_validate(uint32_t slot_base, uint32_t slot_size)
{
    return vector_table_valid(slot_base, slot_size);
}

bool boot_jump_slot(uint32_t slot_base, uint32_t slot_size)
{
    uint32_t sp;
    uint32_t pc;

    if (!boot_image_validate(slot_base, slot_size))
    {
        return false;
    }

    sp = *(volatile uint32_t *)slot_base;
    pc = *(volatile uint32_t *)(slot_base + 4u);

    /* ---- 剥离 Bootloader 环境（顺序对应架构文档 §8.2，不可乱序）---- */

    /* 1. 关全局中断，之后不再有中断进入 Boot 的世界 */
    __disable_irq();

    /* 2/3. 停 SysTick（HAL 层 + 寄存器层，双保险） */
    HAL_SuspendTick();
    SysTick->CTRL = 0u;

    /* 4. 清空 NVIC 使能与 pending（F407 共 82 个 IRQ，覆盖 3 个字） */
    for (uint32_t i = 0u; i < 3u; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }

    /* 5. 清 FPU lazy stacking（架构文档 §8.2 第 4 步） */
    FPU->FPCCR &= ~(FPU_FPCCR_ASPEN_Msk | FPU_FPCCR_LSPEN_Msk);
    __DSB();
    __ISB();

    /* 6. App 向量表接管 */
    SCB->VTOR = slot_base;

    /* 7. 恢复全局中断（PRIMASK=0）。
     *    必须在改 MSP 之前做完：换栈之后 C 语句就不可靠了。 */
    __enable_irq();

    /* 8. 换栈、跳转（此后本函数不返回） */
    __set_MSP(sp);
    ((void (*)(void))pc)();

    return true;  /* 不可达，仅消除编译器警告 */
}
