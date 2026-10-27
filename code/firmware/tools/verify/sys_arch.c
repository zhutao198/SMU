/**
 * @file   sys_arch.c
 * @brief  LwIP OS 抽象层（FreeRTOS 原生 API，独立构建用）
 * @note   替代 CubeMX 生成的 system/OS/sys_arch.c（其依赖 CMSIS-RTOS v1，
 *         而本机 FreeRTOS 中间件的 v1 包装不完整）。实现 LwIP 官方
 *         FreeRTOS 移植的标准接口。
 */
#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/sys.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/stats.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

extern volatile BaseType_t xHigherPriorityTaskWoken;

/* ------------------------- 互斥量（优先级继承，防优先级反转） -------------- */
err_t sys_mutex_new(sys_mutex_t *mutex)
{
    *mutex = xSemaphoreCreateMutex();
    return (*mutex != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
    xSemaphoreTake(*mutex, portMAX_DELAY);
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
    xSemaphoreGive(*mutex);
}

void sys_mutex_free(sys_mutex_t *mutex)
{
    if (*mutex != NULL) {
        vSemaphoreDelete(*mutex);
        *mutex = NULL;
    }
}

int sys_mutex_valid(sys_mutex_t *mutex)
{
    return (*mutex != NULL) ? 1 : 0;
}

void sys_mutex_set_invalid(sys_mutex_t *mutex)
{
    *mutex = NULL;
}

/* ------------------------- valid 查询（LwIP 2.2 新增 API） ----------------- */
int sys_sem_valid(sys_sem_t *sem)
{
    return (*sem != NULL) ? 1 : 0;
}

void sys_sem_set_invalid(sys_sem_t *sem)
{
    *sem = NULL;
}

int sys_mbox_valid(sys_mbox_t *mbox)
{
    return (*mbox != NULL) ? 1 : 0;
}

void sys_mbox_set_invalid(sys_mbox_t *mbox)
{
    *mbox = NULL;
}

#if !NO_SYS

void sys_init(void)
{
    /* LwIP 启动时调用一次；无需额外初始化 */
}

/* ------------------------- 临界区 ------------------------- */
sys_prot_t sys_arch_protect(void)
{
    taskENTER_CRITICAL();
    return (sys_prot_t)1;
}

void sys_arch_unprotect(sys_prot_t pval)
{
    (void)pval;
    taskEXIT_CRITICAL();
}

/* ------------------------- 线程 ------------------------- */
void sys_thread_exit(void)
{
    vTaskDelete(NULL);
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn entry_function,
                            void *arg, int stacksize, int priority)
{
    TaskHandle_t handle = NULL;
    UBaseType_t prio = (UBaseType_t)priority;
    if (prio == 0) prio = configMAX_PRIORITIES - 1;
    xTaskCreate(entry_function, name, (configSTACK_DEPTH_TYPE)stacksize,
                arg, prio, &handle);
    return (sys_thread_t)handle;
}

/* ------------------------- 邮箱 ------------------------- */
err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
    *mbox = xQueueCreate((UBaseType_t)(size > 0 ? size : 1), sizeof(void *));
    return (*mbox != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
    if (*mbox != NULL) {
        vQueueDelete(*mbox);
        *mbox = NULL;
    }
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    BaseType_t xHigher;
    xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendToBackFromISR(*mbox, &msg, &xHigherPriorityTaskWoken);
    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    return (xQueueSend(*mbox, &msg, 0) == errQUEUE_FULL) ? ERR_MEM : ERR_OK;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
    return (xQueueSendFromISR(*mbox, &msg, NULL) == errQUEUE_FULL) ? ERR_MEM : ERR_OK;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
    void *dummy;
    TickType_t start = xTaskGetTickCount();
    if (msg == NULL) msg = &dummy;

    if (timeout == 0) {
        timeout = portMAX_DELAY;
    } else {
        timeout = pdMS_TO_TICKS(timeout);
        if (timeout == 0) timeout = 1;
    }
    if (xQueueReceive(*mbox, &(*msg), timeout) == pdFALSE) {
        if (msg) *msg = NULL;
        return SYS_ARCH_TIMEOUT;
    }
    return (u32_t)(xTaskGetTickCount() - start);
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
    void *dummy;
    if (msg == NULL) msg = &dummy;
    if (xQueueReceive(*mbox, &(*msg), 0) == pdFALSE) {
        if (msg) *msg = NULL;
        return SYS_MBOX_EMPTY;
    }
    return 0;
}

/* ------------------------- 信号量 ------------------------- */
err_t sys_sem_new(sys_sem_t *sem, u8_t initial_count)
{
    *sem = xSemaphoreCreateCounting(0xFFFFFFFF, (UBaseType_t)initial_count);
    return (*sem != NULL) ? ERR_OK : ERR_MEM;
}

void sys_sem_free(sys_sem_t *sem)
{
    if (*sem != NULL) {
        vSemaphoreDelete(*sem);
        *sem = NULL;
    }
}

void sys_sem_set(sys_sem_t *sem)
{
    xSemaphoreGive(*sem);
}

void sys_sem_signal(sys_sem_t *sem)
{
    xSemaphoreGive(*sem);
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
    TickType_t start = xTaskGetTickCount();
    if (timeout == 0) {
        timeout = portMAX_DELAY;
    } else {
        timeout = pdMS_TO_TICKS(timeout);
        if (timeout == 0) timeout = 1;
    }
    if (xSemaphoreTake(*sem, timeout) == pdFALSE) {
        return SYS_ARCH_TIMEOUT;
    }
    return (u32_t)(xTaskGetTickCount() - start);
}

#endif /* !NO_SYS */
