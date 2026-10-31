/**
 * @file   ethernetif.c
 * @brief  以太网底层（STM32F4 HAL ETH + LwIP，独立构建用）
 * @note   按 STM32CubeF4 官方 LwIP 例程（LwIP_TCP_Echo_Server）结构编写：
 *         - 零拷贝收包：HAL 的 RxAllocate/RxLink/TxFree 回调 + custom pbuf 池
 *         - custom pbuf 必须提供**连续且不小于 ETH_RX_BUF_SIZE 的 buffer**
 *           （不能用 pbuf_alloc(PBUF_POOL)：其 buffer 仅 512B，DMA 写整帧会溢出）
 *         - HAL_ETH_Start() 在 lan8720_init() 中（HAL_ETH_Init 之后）无条件启动
 *         - RX 释放修复：rx_pbuf_free 严禁再调 pbuf_free（custom pbuf 会递归死循环，
 *           曾导致 tcpip 线程栈溢出踩坏 ETH DMA 描述符 → AHB 总线锁死、板子冻结）。
 */
#include "main.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/mem.h"

extern ETH_HandleTypeDef heth;
extern struct netif gnetif;

#define ETH_RX_DESC_CNT    4U
#define ETH_TX_DESC_CNT    4U
#define ETH_RX_BUF_SIZE    1524U
#define ETH_TX_BUF_SIZE    1524U

static ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __attribute__((aligned(4)));
static ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __attribute__((aligned(4)));
static uint8_t Tx_Buff[ETH_TX_DESC_CNT][ETH_TX_BUF_SIZE] __attribute__((aligned(4)));

static const uint8_t s_mac[6] = { 0x00, 0x80, 0xE1, 0x12, 0x34, 0x56 };

/* 供 lwip.c 使用 */
err_t ethernetif_init(struct netif *netif);
void  ethernetif_poll(struct netif *netif);
void  lan8720_init(void);

/* ---------------- 零拷贝 RX：自定义 pbuf 池 + 空闲链表 ----------------
 * 设计要点（修复历史"收首包即死机 / AHB 总线锁死"）：
 *  - 原 rx_pbuf_free 直接调用 pbuf_free(p) 形成递归（custom pbuf 释放会再次回调
 *    custom_free_function），导致 tcpip 线程（栈仅 1024B）栈溢出、踩坏 ETH DMA
 *    描述符 → AHB 总线锁死、J-Link 无法附着。现改为仅把 buffer 归还空闲链表。
 *  - 采用空闲链表管理 buffer：RxAllocate 出队一个、RxFree 归还一个，避免 round-robin
 *    把仍被 LwIP 持有的 buffer 又交给 DMA 覆盖。
 */
#define RX_POOL_CNT  12U

typedef struct {
    struct pbuf_custom p;
    uint8_t buff[ETH_RX_BUF_SIZE];
} rx_pbuf_t;

static rx_pbuf_t s_rx_pool[RX_POOL_CNT];
/* 空闲链表（存放 pool 下标）：RxAllocate 出队、RxFree 入队 */
static uint8_t s_rx_free[RX_POOL_CNT];
static uint8_t s_rx_free_head;
static uint8_t s_rx_free_tail;
static uint8_t s_rx_free_cnt;

static void rx_pbuf_free(struct pbuf *p)
{
    /* custom pbuf 释放：LwIP 调用 custom_free_function 时 pbuf 引用已归零，
     * 此处严禁再调 pbuf_free（否则无限递归）。只需把对应 buffer 归还空闲链表。 */
    rx_pbuf_t *r = (rx_pbuf_t *)((uint8_t *)p - offsetof(rx_pbuf_t, p));
    uint32_t idx = (uint32_t)(r - s_rx_pool);
    if (idx < RX_POOL_CNT) {
        s_rx_free[s_rx_free_tail] = (uint8_t)idx;
        s_rx_free_tail = (uint8_t)((s_rx_free_tail + 1U) % RX_POOL_CNT);
        s_rx_free_cnt++;
    }
}

static void rx_pool_init(void)
{
    s_rx_free_head = 0;
    s_rx_free_tail = 0;
    s_rx_free_cnt  = 0;
    for (uint8_t i = 0; i < RX_POOL_CNT; i++) {
        s_rx_pool[i].p.custom_free_function = rx_pbuf_free;
        s_rx_free[s_rx_free_tail] = i;
        s_rx_free_tail = (uint8_t)((s_rx_free_tail + 1U) % RX_POOL_CNT);
        s_rx_free_cnt++;
    }
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    if (s_rx_free_cnt == 0U) {
        *buff = NULL;   /* 无空闲 buffer：HAL 会保留旧描述符，下次接收再补 */
        return;
    }
    uint8_t idx = s_rx_free[s_rx_free_head];
    s_rx_free_head = (uint8_t)((s_rx_free_head + 1U) % RX_POOL_CNT);
    s_rx_free_cnt--;
    *buff = s_rx_pool[idx].buff;
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
    struct pbuf **pp    = (struct pbuf **)pStart;
    struct pbuf **ppEnd = (struct pbuf **)pEnd;
    rx_pbuf_t *r = (rx_pbuf_t *)(buff - offsetof(rx_pbuf_t, buff));
    struct pbuf *p = pbuf_alloced_custom(PBUF_RAW, Length, PBUF_REF, &r->p, buff, ETH_RX_BUF_SIZE);
    if (p == NULL) return;

    p->next = NULL;
    if (*pp == NULL) *pp = p;
    else (*ppEnd)->next = p;
    *ppEnd = p;
}

void HAL_ETH_TxFreeCallback(uint32_t *buff)
{
    pbuf_free((struct pbuf *)buff);
}

/* 调试计数器（J-Link 可读，定位收发包断点；M1 调试用，可保留） */
volatile uint32_t g_dbg_rx_pkts   = 0;   /* 成功从 DMA 读到并交给 LwIP 的包数 */
volatile uint32_t g_dbg_rx_fail   = 0;   /* HAL_ETH_ReadData 返回失败次数      */
volatile uint32_t g_dbg_in_err    = 0;   /* netif->input 返回非 OK 次数        */
volatile uint32_t g_dbg_tx_pkts   = 0;   /* low_level_output 成功发包数         */
volatile uint32_t g_dbg_tx_fail   = 0;   /* low_level_output 失败次数           */

/* ---------------- 收包/发包 ---------------- */
static ETH_TxPacketConfigTypeDef s_txcfg;

static struct pbuf *low_level_input(struct netif *netif)
{
    void *appbuff = NULL;
    if (HAL_ETH_ReadData(&heth, &appbuff) != HAL_OK) { g_dbg_rx_fail++; return NULL; }
    if (appbuff == NULL) return NULL;
    return (struct pbuf *)appbuff;
}

err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    ETH_BufferTypeDef txbuf[ETH_TX_DESC_CNT];
    uint32_t i = 0, len = 0;
    struct pbuf *q;

    memset(txbuf, 0, sizeof(txbuf));
    for (q = p; q != NULL; q = q->next) {
        if (i >= ETH_TX_DESC_CNT) return ERR_BUF;
        txbuf[i].buffer = q->payload;
        txbuf[i].len = q->len;
        if (i > 0) txbuf[i - 1].next = &txbuf[i];
        if (q->next == NULL) txbuf[i].next = NULL;
        len += q->len;
        i++;
    }
    s_txcfg.Length = len;
    s_txcfg.TxBuffer = txbuf;
    s_txcfg.pData = p;
    if (HAL_ETH_Transmit(&heth, &s_txcfg, 100) != HAL_OK) { g_dbg_tx_fail++; return ERR_BUF; }
    g_dbg_tx_pkts++;
    return ERR_OK;
}

/* 轮询收包：喂给 tcpip 线程 */
void ethernetif_poll(struct netif *netif)
{
    struct pbuf *p;
    while ((p = low_level_input(netif)) != NULL) {
        g_dbg_rx_pkts++;
        if (netif->input(p, netif) != ERR_OK) {
            g_dbg_in_err++;
            pbuf_free(p);
        }
    }
}

/* ---------------- 初始化 ---------------- */
static err_t low_level_init(struct netif *netif)
{
    ETH_MACConfigTypeDef macconf;

    heth.Instance = ETH;
    heth.Init.MACAddr = (uint8_t *)s_mac;
    heth.Init.MediaInterface = ETH_MEDIA_INTERFACE_RMII;
    heth.Init.RxDesc = DMARxDscrTab;
    heth.Init.TxDesc = DMATxDscrTab;
    heth.Init.RxBuffLen = ETH_RX_BUF_SIZE;

    if (HAL_ETH_Init(&heth) != HAL_OK) return ERR_IF;

    HAL_ETH_RegisterRxAllocateCallback(&heth, HAL_ETH_RxAllocateCallback);
    HAL_ETH_RegisterRxLinkCallback(&heth, HAL_ETH_RxLinkCallback);
    HAL_ETH_RegisterTxFreeCallback(&heth, HAL_ETH_TxFreeCallback);

    if (HAL_ETH_GetMACConfig(&heth, &macconf) == HAL_OK) {
        macconf.ChecksumOffload = ETH_CHECKSUMOFFLAOD_ENABLE;
        HAL_ETH_SetMACConfig(&heth, &macconf);
    }

    /* TxConfig 公共参数（官方模板） */
    memset(&s_txcfg, 0, sizeof(s_txcfg));
    s_txcfg.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
    /* 旁路 MAC 硬件校验和插入（已由 netif->chksum_flags 让 LwIP 软件计算，
     * 避免硬件对链式 pbuf 算出错误校验和）。CRC/PAD 仍由 MAC 插入。 */
    s_txcfg.ChecksumCtrl = ETH_CHECKSUM_DISABLE;
    s_txcfg.CRCPadCtrl = ETH_CRC_PAD_INSERT;

    MEMCPY(netif->hwaddr, s_mac, 6);
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    /* 关键修复：本工程 LWIP_CHECKSUM_CTRL_PER_NETIF=1，是否算校验和完全由
     * netif->chksum_flags 决定。原先未设置（=0）→ LwIP 跳过所有软件校验和，
     * 仅靠 MAC 硬件 ChecksumCtrl=FULL 插入；而该硬件插入对"链式 pbuf"（以太网头
     * 与 IP 载荷分属不同 pbuf）算出的 IP/ICMP 校验和错误，PC 据此丢弃 ICMP 回包
     * （ARP 无 IP 校验和故正常，表现为"ARP 通、ping 不通"）。
     * 这里置上 GEN 位让 LwIP 软件计算校验和（跨链式 pbuf 正确），并把 MAC 改为旁路。 */
    netif->chksum_flags = NETIF_CHECKSUM_GEN_IP | NETIF_CHECKSUM_GEN_UDP
                        | NETIF_CHECKSUM_GEN_TCP | NETIF_CHECKSUM_GEN_ICMP;

    /* RX buffer 空闲链表必须在 HAL_ETH_Start 之前初始化：
     * HAL_ETH_Start -> ETH_UpdateDescriptor 会调用 RxAllocateCallback 为每个 RX
     * 描述符分配 buffer，空闲链表未就绪则描述符拿不到 buffer（DESC2=0）导致 DMA
     * 写地址 0 -> 总线错误。 */
    rx_pool_init();

    /* PHY 复位 + 自协商，并在内部（HAL_ETH_Init 之后）无条件启动 MAC/DMA。 */
    lan8720_init();

    return ERR_OK;
}

err_t ethernetif_init(struct netif *netif)
{
    netif->name[0] = 'e';
    netif->name[1] = '0';
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    return low_level_init(netif);
}
