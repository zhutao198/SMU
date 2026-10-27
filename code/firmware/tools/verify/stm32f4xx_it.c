/**
 * @file   stm32f4xx_it.c
 * @brief  中断服务（独立构建用；CubeMX 工程请用其生成的同名文件）
 * @note   本项目的 ETH 使用轮询收包（ethernetif_poll），ETH 中断仅作错误提示。
 */
#include "main.h"
#include "stm32f4xx_hal.h"

extern TIM_HandleTypeDef htim2;
extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;
extern ETH_HandleTypeDef heth;

/* FreeRTOS 的 SysTick 处理（port.c 中的真实函数名） */
extern void xPortSysTickHandler(void);

/**
 * @brief SysTick：同时喂 HAL（uwTick，HAL_Delay 用）和 FreeRTOS（任务调度）
 * @note  二者缺一不可：只接 FreeRTOS 会导致 HAL_Delay 死等；
 *        只接 HAL 会导致任务不切换。
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
    xPortSysTickHandler();
}

void NMI_Handler(void) { while (1) {} }
void HardFault_Handler(void) { while (1) {} }
void MemManage_Handler(void) { while (1) {} }
void BusFault_Handler(void) { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }

void DebugMon_Handler(void) {}

void TIM2_IRQHandler(void) { HAL_TIM_IRQHandler(&htim2); }
void I2C1_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hi2c1); }
void I2C1_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hi2c1); }
void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }
void ETH_IRQHandler(void) { HAL_ETH_IRQHandler(&heth); }
