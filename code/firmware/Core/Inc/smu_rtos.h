/**
 * @file   smu_rtos.h
 * @brief  FreeRTOS 兼容层（FreeRTOS 10.3 起 pdMS_TO_TICKS 被移除，此处兜底）
 */
#ifndef SMU_RTOS_H
#define SMU_RTOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((TickType_t)(((uint32_t)(ms) * configTICK_RATE_HZ) / 1000U))
#endif

#ifndef pdTICKS_TO_MS
#define pdTICKS_TO_MS(t) ((uint32_t)(((uint32_t)(t) * 1000U) / configTICK_RATE_HZ))
#endif

#ifdef __cplusplus
}
#endif

#endif /* SMU_RTOS_H */
