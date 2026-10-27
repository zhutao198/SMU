/**
 * @file   main.h
 * @brief  外设句柄声明（独立构建用；CubeMX 工程请用其生成的 main.h）
 */
#ifndef __MAIN_H
#define __MAIN_H

#include "stm32f4xx_hal.h"

void Error_Handler(void);

extern TIM_HandleTypeDef  htim2;
extern I2C_HandleTypeDef  hi2c1;
extern UART_HandleTypeDef huart1;
extern IWDG_HandleTypeDef hiwdg;
extern ETH_HandleTypeDef  heth;

/* CubeMX 等价初始化入口（tools/verify 下由各模块提供；
   CubeMX 工程中这些声明由其自动生成的 main.h 提供） */
void MX_GPIO_Init(void);
void MX_I2C1_Init(void);
void MX_TIM2_Init(void);
void MX_USART1_UART_Init(void);
void MX_IWDG_Init(void);
void MX_ETH_Init(void);
void MX_LWIP_Init(void);
void MX_FREERTOS_Init(void);

#endif /* __MAIN_H */
