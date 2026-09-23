/**
 ******************************************************************************
 * @file    boot_jump.c
 * @brief   跳转到目标槽的实现
 ******************************************************************************
 */
#include "boot_jump.h"

#include "boot_conf.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "ota_image.h"
#include "stm32f4xx_hal.h"

/** NVIC 的 ICER / ICPR 寄存器个数，Cortex-M4 最多 240 个外部中断，够用 */
#define BOOT_NVIC_REGISTER_COUNT   (8u)

bool boot_jump_vector(uint32_t slot_id, uint32_t image_size, boot_jump_vector_t *vector)
{
    uint32_t base = boot_flash_slot_base(slot_id);
    uint32_t sp = 0u;
    uint32_t pc = 0u;

    if ((base == 0u) || (vector == NULL) || (image_size == 0u))
    {
        return false;
    }

    if (!boot_image_vector_valid(base + OTA_IMAGE_LOAD_OFFSET,
                                 base,
                                 image_size,
                                 &sp,
                                 &pc))
    {
        return false;
    }

    vector->vtor = base + OTA_IMAGE_LOAD_OFFSET;
    vector->sp = sp;
    vector->pc = pc;

    return true;
}

void boot_jump_to(const boot_jump_vector_t *vector)
{
    uint32_t index;

    if (vector == NULL)
    {
        boot_jump_restart();
        return;
    }

    /* 先把中断全部关掉并清掉挂起位：Boot 期间可能已经有 UART 中断挂在那里，
       带着它跳过去，App 一开中断就会跑进 Boot 的向量表。 */
    __disable_irq();

    for (index = 0u; index < BOOT_NVIC_REGISTER_COUNT; index++)
    {
        NVIC->ICER[index] = 0xFFFFFFFFu;
        NVIC->ICPR[index] = 0xFFFFFFFFu;
    }

    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;

    /* VTOR 必须在切栈之前设好：切栈之后执行的每一条指令都从新向量表取异常入口。 */
    SCB->VTOR = vector->vtor;
    __DSB();
    __ISB();

    __set_CONTROL(0u);
    __set_MSP(vector->sp);
    __DSB();
    __ISB();

    ((void (*)(void))vector->pc)();

    /* 目标镜像的复位入口正常不会返回。 */
    for (;;)
    {
    }
}

void boot_jump_restart(void)
{
    __disable_irq();
    NVIC_SystemReset();

    /* NVIC_SystemReset() 正常不会返回。 */
    for (;;)
    {
    }
}
