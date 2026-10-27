/**
 * @file   tim2.c
 * @brief  MX_TIM2_Init：CH2 PWM（PB3），~100kHz（PRD §5 O-2 实测后定）
 *         TIM2CLK = 84MHz（APB1×2）；ARR=840, PSC=0 → f ≈ 100kHz
 */
#include "main.h"

TIM_HandleTypeDef htim2;

void MX_TIM2_Init(void)
{
  TIM_OC_InitTypeDef oc = {0};

  __HAL_RCC_TIM2_CLK_ENABLE();

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  /* PWM 频率 = 84MHz/(PSC+1)/(ARR+1)
   * 100kHz(ARR=839) 时纹波电流 ΔI=Vin·D(1-D)/(L·f) ≈ 4.3A，超过 L3(10µH/1.5A)
   * 额定 → 磁芯饱和 → 电感量骤降 → 电流失控 → 严重发热（2026-10-27 实测）。
   * 400kHz(ARR=209) 时 ΔI ≈ 1.07A，降低 4 倍。
   * M2 硬件改版建议：L3 用 22~47µH/2A，或提高开关频率设计。 */
  htim2.Init.Period = 210 - 1;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) Error_Handler();

  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = 0;
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_2) != HAL_OK) Error_Handler();
}

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim)
{
  GPIO_InitTypeDef g = {0};
  if (htim->Instance != TIM2) return;
  __HAL_RCC_GPIOB_CLK_ENABLE();
  g.Pin = GPIO_PIN_3;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOB, &g);
}
