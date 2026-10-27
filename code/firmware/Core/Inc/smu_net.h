/**
 * @file   smu_net.h
 * @brief  平台网络轮询接口（由平台层实现，应用层只调 SMU_NetPoll）
 * @note   独立构建：tools/verify/smu_net.c（轮询收包 + PHY 状态）
 *         CubeMX 工程：若用以太网中断收包，可提供空实现
 */
#ifndef SMU_NET_H
#define SMU_NET_H

#ifdef __cplusplus
extern "C" {
#endif

void SMU_NetInit(void);          /* 平台网络初始化（tcpip+netif，须在任务上下文调用） */
void SMU_NetPoll(void);          /* 快周期（~5ms）：收包 */
void SMU_NetPhyPoll(void);       /* 慢周期（~100ms）：PHY 链接状态 */
int  SMU_NetPhyProbe(uint8_t addr7, uint32_t *id1, uint32_t *id2); /* PHY 探测：0=成功 */

#ifdef __cplusplus
}
#endif

#endif /* SMU_NET_H */
