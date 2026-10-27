/**
 * @file   sys_now.c
 * @brief  LwIP sys_now()（独立构建用；CubeMX 工程由其生成代码提供）
 */
#include "lwip/sys.h"
#include "lwip/arch.h"
#include "FreeRTOS.h"
#include "task.h"

u32_t sys_now(void)
{
    /* configTICK_RATE_HZ = 1000 → 1 tick = 1ms */
    return (u32_t)(xTaskGetTickCount() * (1000U / configTICK_RATE_HZ));
}
