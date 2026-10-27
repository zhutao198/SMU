/**
 * @file   smu_uart.c
 * @brief  USART1：中断接收环形缓冲 + 行组装 + 发送（FR-COMM-1）
 */
#include "smu_uart.h"
#include "smu_rtos.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

static volatile uint8_t s_rx_ring[SMU_UART_RX_RING];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static uint8_t s_rx_byte;

void SMU_UartRxCb(uint8_t byte)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % SMU_UART_RX_RING);
    if (next != s_rx_tail) {
        s_rx_ring[s_rx_head] = byte;
        s_rx_head = next;
    }
}

void SMU_UartInit(void)
{
    /* 幂等：main() 在 MX_USART1_UART_Init 之后已提前装填接收中断，
     * 此处若再清空环形缓冲，会丢掉已收到的字节——表现为"上电后第一条命令
     * 首字符丢失 → -100 Command error，第二次才正常"。
     * 故：不清缓冲，仅在未接收时补装填。 */
    if (huart1.RxState != HAL_UART_STATE_BUSY_RX) {
        s_rx_byte = 0;
        HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
    }
}

/* HAL 接收完成回调：存入环形缓冲并重新装填 */
extern volatile uint32_t g_dbg_rx_bytes;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;
    g_dbg_rx_bytes++;
    SMU_UartRxCb(s_rx_byte);
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
}

/**
 * @brief USART 错误回调（ORE/FE/NE）
 * @note  必须实现：默认的弱定义为空，一旦发生溢出错误（RXNE 未及时读走），
 *        USART 会停止接收且永不恢复，表现为"命令无响应"。
 *        这里清错误标志并重新装填接收。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;

    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    huart->ErrorCode = HAL_UART_ERROR_NONE;
    huart->RxState = HAL_UART_STATE_READY;
    huart->Lock = HAL_UNLOCKED;

    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
}

int SMU_UartGetLine(char *buf, uint32_t len, uint32_t timeout_ms)
{
    uint32_t start   = HAL_GetTick();
    uint32_t last_rx = HAL_GetTick();   /* 最近一次收到字符的时刻 */
    uint32_t n = 0;
    while ((HAL_GetTick() - start) < timeout_ms) {
        /* RX 健康兜底：若 HAL 接收状态异常（错误/未运行），重新装填。
         * 防止 ORE 等原因导致"命令无响应"且 ErrorCallback 未覆盖的情况。 */
        if (huart1.RxState != HAL_UART_STATE_BUSY_RX) {
            HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
        }
        while (s_rx_tail != s_rx_head) {
            uint8_t c = s_rx_ring[s_rx_tail];
            s_rx_tail = (uint16_t)((s_rx_tail + 1U) % SMU_UART_RX_RING);
            last_rx = HAL_GetTick();
            if (c == '\n' || c == '\r') {
                if (n == 0) continue;                  /* 忽略空行 */
                buf[n] = '\0';
                return 0;
            }
            if (n + 1U < len) buf[n++] = (char)c;
        }
        /* 兼容不带换行符的串口工具：收到半行字符后一段时间无新数据即视为一行。
         * 200ms 足够宽容：粘贴/按钮发送是瞬时的（<10ms 成行），
         * 逐字符手动输入（间隔 <200ms）也不会被拆断。 */
        if (n > 0U && (HAL_GetTick() - last_rx) > 200U) {
            buf[n] = '\0';
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return -1;
}

void SMU_UartSend(const char *s, uint32_t len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)len, 100);
}
