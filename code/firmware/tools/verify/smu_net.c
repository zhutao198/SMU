/**
 * @file   smu_net.c
 * @brief  平台网络轮询实现（独立构建用；CubeMX 工程请按其中间件替换）
 */
#include "main.h"
#include "lwip/netif.h"

extern struct netif gnetif;
extern ETH_HandleTypeDef heth;
void ethernetif_poll(struct netif *netif);
void lan8720_poll(struct netif *netif);
void MX_LWIP_Init(void);

/* PHY 探测：读指定地址的 PHY ID 寄存器（reg2/reg3），0=成功 */
int SMU_NetPhyProbe(uint8_t addr7, uint32_t *id1, uint32_t *id2)
{
    if (addr7 > 31U) return -1;
    if (HAL_ETH_ReadPHYRegister(&heth, (uint32_t)addr7, 0x02U, id1) != HAL_OK) return -1;
    if (HAL_ETH_ReadPHYRegister(&heth, (uint32_t)addr7, 0x03U, id2) != HAL_OK) return -1;
    return 0;
}

/* 平台网络初始化：必须在 FreeRTOS 任务上下文调用。
 * 原因：netif_add -> ethernetif_init -> HAL_ETH_Init 内部用 HAL_Delay 等
 * SysTick，而调度器启动前的堆分配会残留 BASEPRI 屏蔽 SysTick，导致死等。 */
void SMU_NetInit(void)
{
    MX_LWIP_Init();
}

void SMU_NetPoll(void)
{
    ethernetif_poll(&gnetif);
}

void SMU_NetPhyPoll(void)
{
    lan8720_poll(&gnetif);
}
