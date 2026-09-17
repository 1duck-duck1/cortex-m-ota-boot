/**
 ******************************************************************************
 * @file    app_main.c
 * @brief   fake_app：仅用于验证 Boot↔App 双向跳转的最小应用
 *
 * 链接在 Slot A（0x08010000），不参与 Boot target 的编译。
 * 行为：上电 → 确认（清启动计数）→ 心跳 → 数秒后主动请求回 Boot。
 ******************************************************************************
 */
#include "stm32f4xx_hal.h"
#include "boot_client.h"
#include "boot_conf.h"

/* 上电后自动回跳 Boot 的延时（毫秒）。
 * 演示 F-03 反向跳转用；置 0 则一直运行不回跳。 */
#define FAKE_APP_AUTO_REBOOT_MS   5000u

static void SystemClock_Config(void);
static void Error_Handler(void);

int main(void)
{
    /* App 起步三件事，顺序不可换：
     * 1) 向量表重定位到本槽（SystemInit 默认设的 FLASH_BASE 是 Boot 的） */
    SCB->VTOR = BOOT_SLOT_A_ADDR;

    /* 2) Boot 跳转前关过全局中断（PRIMASK=1），必须恢复，否则一切中断失灵 */
    __enable_irq();

    /* 3) 常规初始化（与 Boot 相同的 HSE 8MHz → 168MHz 配置） */
    HAL_Init();
    SystemClock_Config();

    boot_client_init();

    /* 自检"通过"后确认（真实工程应做完关键外设自检再确认），
     * Boot 下次上电读到确认即清零启动计数 */
    boot_client_confirm();

    uint32_t started = HAL_GetTick();
    while (1)
    {
        /* 心跳：调试器观察 BKPR3 递增即知 App 活着 */
        boot_client_heartbeat();

        if (FAKE_APP_AUTO_REBOOT_MS != 0u &&
            (HAL_GetTick() - started) >= FAKE_APP_AUTO_REBOOT_MS)
        {
            boot_client_request_update();  /* 演示 F-03，触发复位，不返回 */
        }

        HAL_Delay(100);
    }
}

/** 与 CubeMX 生成的 Boot 工程完全相同时钟：HSE 8MHz → PLL → 168MHz */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 168;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }
}

static void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}
