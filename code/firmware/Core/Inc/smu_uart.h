/**
 * @file   smu_uart.h
 * @brief  USART1 中断接收（环形缓冲）+ 行组装 + 发送
 */
#ifndef SMU_UART_H
#define SMU_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32f4xx_hal.h"

#define SMU_UART_RX_RING   256U
#define SMU_LINE_MAX       96U

void SMU_UartInit(void);
/* 取一行（LF 结尾，已去 CR/LF）；返回 0=成功 */
int  SMU_UartGetLine(char *buf, uint32_t len, uint32_t timeout_ms);
void SMU_UartSend(const char *s, uint32_t len);
/* 供 HAL 回调使用 */
void SMU_UartRxCb(uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* SMU_UART_H */
