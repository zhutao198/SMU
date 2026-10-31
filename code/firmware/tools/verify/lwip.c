/**
 * @file   lwip.c
 * @brief  MX_LWIP_Init：tcpip + netif（IP 来自 NV 配置，M1 为静态 IP）
 */
#include "main.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "smu_cal.h"

extern struct netif gnetif;
struct netif gnetif;
extern err_t ethernetif_init(struct netif *netif);
extern volatile uint32_t g_dbg_stage;

void MX_LWIP_Init(void)
{
  ip4_addr_t ip, mask, gw;
  const smu_nv_t *nv = SMU_CalNv();

  g_dbg_stage = 100;
  /* 强制使能 ETH 时钟：实测运行时 RCC AHB1ENR 的 ETH 时钟位(22/23/24)为 0、
   * MACCR/DMAOMR 全 0 —— 说明 HAL_ETH_Init 未能打开时钟（或其内部使能未生效），
   * 导致 MAC 寄存器不可访问、初始化失败。此处 netif_add 之前显式打开，覆盖
   * F407(F407:22/23/24) 与 F479(25/26/27) 两种位布局，确保后续 HAL_ETH_Init 能访问外设。 */
  RCC->AHB1ENR |= (0x7FUL << 22);   /* bit22..28: ETHMAC/TX/RX/PTP(及 F479 的 25/26/27) */
  tcpip_init(NULL, NULL);
  g_dbg_stage = 101;

  IP4_ADDR(&ip,   nv->ip[0],      nv->ip[1],      nv->ip[2],      nv->ip[3]);
  IP4_ADDR(&mask, nv->netmask[0], nv->netmask[1], nv->netmask[2], nv->netmask[3]);
  IP4_ADDR(&gw,   nv->gw[0],      nv->gw[1],      nv->gw[2],      nv->gw[3]);

  struct netif *n = netif_add(&gnetif, &ip, &mask, &gw, NULL, &ethernetif_init, &tcpip_input);
  if (n == NULL) { g_dbg_stage = 103; return; }   /* init 失败（HAL_ETH_Init 等），保留现场待查 */
  netif_set_default(&gnetif);
  netif_set_up(&gnetif);
  g_dbg_stage = 102;
}
