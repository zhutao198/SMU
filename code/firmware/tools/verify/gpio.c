/**
 * @file   gpio.c
 * @brief  MX_GPIO_Init（独立构建用，对应 README §2 CubeMX 引脚清单）
 */
#include "main.h"
#include "smu_board.h"

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* ---- 控制/状态 GPIO ---- */
  g.Pin = SMU_PWM_PIN;                 /* PB3  TIM2_CH2 */
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(SMU_PWM_PORT, &g);

  g.Pin = SMU_EN_PIN;                  /* PB4  VOUT_EN */
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Alternate = 0;
  HAL_GPIO_Init(SMU_EN_PORT, &g);

  g.Pin = SMU_ITEST_PIN;               /* PD1  I_TEST_EN */
  HAL_GPIO_Init(SMU_ITEST_PORT, &g);
  g.Pin = SMU_IVTEST_PIN;              /* PD2  I_V_TEST_EN */
  HAL_GPIO_Init(SMU_IVTEST_PORT, &g);

  g.Pin = SMU_RDY_PIN;                 /* PB5  ADS1115 RDY，上拉输入 */
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SMU_RDY_PORT, &g);

  g.Pin = SMU_J18_1_PIN | SMU_J18_2_PIN; /* PC11/PC12 拨码，内部上拉 */
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SMU_J18_1_PORT, &g);

  /* ---- I2C1 PB6/PB7 ---- */
  g.Pin = GPIO_PIN_6 | GPIO_PIN_7;
  g.Mode = GPIO_MODE_AF_OD;
  g.Pull = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &g);

  /* ---- USART1 PA9/PA10 ---- */
  g.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOA, &g);

  /* ---- ETH RMII ---- */
  g.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;   /* REF_CLK / MDIO / CRS_DV */
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(GPIOA, &g);

  g.Pin = GPIO_PIN_1;                           /* PC1 MDC */
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(GPIOC, &g);

  g.Pin = GPIO_PIN_4 | GPIO_PIN_5;              /* PC4 RXD0 / PC5 RXD1 */
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(GPIOC, &g);

  g.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13; /* TX_EN / TXD0 / TXD1 */
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(GPIOB, &g);
}
