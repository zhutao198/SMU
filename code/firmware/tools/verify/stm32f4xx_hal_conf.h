/**
 * @file   stm32f4xx_hal_conf.h
 * @brief  HAL 模块配置（验证构建用；CubeMX 工程请用其自动生成的同名文件）
 * @note   启用模块与 README §2 CubeMX 清单保持一致。
 */
#ifndef __STM32F4xx_HAL_CONF_H
#define __STM32F4xx_HAL_CONF_H

#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_ETH_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#define HSE_VALUE            8000000U
#define HSI_VALUE            16000000U
#define LSE_VALUE            32768U
#define LSI_VALUE            32000U
#define EXTERNAL_CLOCK_VALUE 12288000U
#define HSE_STARTUP_TIMEOUT  100U
#define LSE_STARTUP_TIMEOUT  5000U
#define VDD_VALUE            3300U
#define TICK_INT_PRIORITY    15U
#define USE_RTOS             0U
#define PREFETCH_ENABLE      1U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE    1U
#define PHY_READ_TO          0x0000FFFFU
#define PHY_WRITE_TO         0x0000FFFFU

#define ETH_RXBUFNB          4U
#define ETH_TXBUFNB          4U
#define ETH_RX_BUF_SIZE      ETH_MAX_PACKET_SIZE
#define ETH_TX_BUF_SIZE      ETH_MAX_PACKET_SIZE
#define ETH_RX_BUF_NB        4U
#define ETH_TX_BUF_NB        4U

/* assert_param：与 CubeMX 生成的默认 hal_conf.h 一致（未定义 USE_FULL_ASSERT 时为空操作） */
#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0U)
#endif

/* PHY MDIO 超时：默认 0xFFFF = 65.5 秒。PHY 无响应时一次访问会霸占 CPU
   65 秒，饿死低优先级任务。改为 100ms。 */
#define PHY_READ_TO    100U
#define PHY_WRITE_TO   100U

#include "stm32f4xx_hal_rcc.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_exti.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_cortex.h"
#include "stm32f4xx_hal_adc.h"
#include "stm32f4xx_hal_eth.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_i2c.h"
#include "stm32f4xx_hal_iwdg.h"
#include "stm32f4xx_hal_pwr.h"
#include "stm32f4xx_hal_tim.h"
#include "stm32f4xx_hal_uart.h"

#endif /* __STM32F4xx_HAL_CONF_H */
