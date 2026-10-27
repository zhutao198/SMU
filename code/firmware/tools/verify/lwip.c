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
  tcpip_init(NULL, NULL);
  g_dbg_stage = 101;

  IP4_ADDR(&ip,   nv->ip[0],      nv->ip[1],      nv->ip[2],      nv->ip[3]);
  IP4_ADDR(&mask, nv->netmask[0], nv->netmask[1], nv->netmask[2], nv->netmask[3]);
  IP4_ADDR(&gw,   nv->gw[0],      nv->gw[1],      nv->gw[2],      nv->gw[3]);

  netif_add(&gnetif, &ip, &mask, &gw, NULL, &ethernetif_init, &tcpip_input);
  netif_set_default(&gnetif);
  netif_set_up(&gnetif);
  g_dbg_stage = 102;
}
