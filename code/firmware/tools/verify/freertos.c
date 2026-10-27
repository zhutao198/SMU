/**
 * @file   freertos.c
 * @brief  内核启动占位（任务由 SMU_AppInit 创建，main.c 调 vTaskStartScheduler）
 */
#include "FreeRTOS.h"
#include "task.h"

void MX_FREERTOS_Init(void)
{
    /* 内核由 vTaskStartScheduler() 启动；此处保留 CubeMX 同名入口 */
}
