/**
 * @file   iwdg.c
 * @brief  MX_IWDG_Init：约 100ms（LSI 32kHz / 32 / 100）（PRD §7 SR-6）
 * @note   【临时】诊断期间不启动 IWDG（否则卡点会导致 100ms 复位循环，
 *         J-Link 无法连接）。定位并修复卡点后恢复。
 */
#include "main.h"

IWDG_HandleTypeDef hiwdg;

void MX_IWDG_Init(void)
{
#if SMU_IWDG_ENABLE
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 100;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) Error_Handler();
#endif
}
