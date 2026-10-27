/**
 * @file   eth.c
 * @brief  MX_ETH_Init：ETH 外设时钟与中断（实际 MAC 初始化在 ethernetif.c）
 */
#include "main.h"

ETH_HandleTypeDef heth;

void MX_ETH_Init(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(ETH_IRQn);
}
