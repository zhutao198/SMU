/**
 * @file   freertos_hooks.c
 * @brief  FreeRTOS 钩子（独立构建用；CubeMX 工程由其生成代码提供）
 */
#include "FreeRTOS.h"
#include "task.h"

/* 调试阶段标记：SMU_AppInit / main 各步更新，hook 里记录，
 * 用 J-Link 读 g_dbg_stage / g_dbg_hook_stage 即可定位失败点 */
volatile uint32_t g_dbg_stage = 0;
volatile uint32_t g_dbg_hook_stage = 0;
volatile uint32_t g_dbg_hook_lr = 0;

#if (configUSE_MALLOC_FAILED_HOOK == 1)
void vApplicationMallocFailedHook(void)
{
    g_dbg_hook_stage = g_dbg_stage;
    g_dbg_hook_lr = (uint32_t)__builtin_return_address(0);
    taskDISABLE_INTERRUPTS();
    for (;;) { }
}
#endif

#if (configCHECK_FOR_STACK_OVERFLOW > 0)
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    g_dbg_hook_stage = 0xDEAD0001;
    taskDISABLE_INTERRUPTS();
    for (;;) { }
}
#endif

#if (configUSE_IDLE_HOOK == 1)
void vApplicationIdleHook(void) { }
#endif
