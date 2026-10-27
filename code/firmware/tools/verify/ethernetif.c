/**
 * @file   ethernetif.c
 * @brief  以太网底层（STM32F4 HAL ETH + LwIP，独立构建用）
 * @note   按 STM32CubeF4 官方 LwIP 例程（LwIP_TCP_Echo_Server）结构编写：
 *         - 零拷贝收包：HAL 的 RxAllocate/RxLink/TxFree 回调 + custom pbuf 池
 *         - custom pbuf 必须提供**连续且不小于 ETH_RX_BUF_SIZE 的 buffer**
 *           （不能用 pbuf_alloc(PBUF_POOL)：其 buffer 仅 512B，DMA 写整帧会溢出）
 *         - HAL_ETH_Start() 在 PHY 链路建立后调用（见 lan8720_poll）
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

/* ---------------- 零拷贝 RX：自定义 pbuf 池 ---------------- */
#define RX_POOL_CNT  6U

typedef struct {
    struct pbuf_custom p;
    uint8_t buff[ETH_RX_BUF_SIZE];
} rx_pbuf_t;

static rx_pbuf_t s_rx_pool[RX_POOL_CNT];
static uint8_t s_rx_idx;

static void rx_pbuf_free(struct pbuf *p)
{
    /* 静态池：无需真正释放（由 alloc 轮转复用） */
    (void)p;
    pbuf_free(p);
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    rx_pbuf_t *r = &s_rx_pool[s_rx_idx];
    s_rx_idx = (uint8_t)((s_rx_idx + 1U) % RX_POOL_CNT);

    *buff = r->buff;
    r->p.custom_free_function = rx_pbuf_free;
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, &r->p, r->buff, ETH_RX_BUF_SIZE);
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
    struct pbuf **pp = (struct pbuf **)pStart;
    struct pbuf **ppEnd = (struct pbuf **)pEnd;
    struct pbuf *p = (struct pbuf *)(buff - offsetof(rx_pbuf_t, buff));

    p->next = NULL;
    p->len = Length;
    p->tot_len = Length;

    if (*pp == NULL) {
        *pp = p;
    } else {
        (*ppEnd)->next = p;
    }
    *ppEnd = p;
}

void HAL_ETH_TxFreeCallback(uint32_t *buff)
{
    pbuf_free((struct pbuf *)buff);
}

/* ---------------- 收包/发包 ---------------- */
static ETH_TxPacketConfigTypeDef s_txcfg;

static struct pbuf *low_level_input(struct netif *netif)
{
    void *appbuff = NULL;
    if (HAL_ETH_ReadData(&heth, &appbuff) != HAL_OK) return NULL;
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
    if (HAL_ETH_Transmit(&heth, &s_txcfg, 100) != HAL_OK) return ERR_BUF;
    return ERR_OK;
}

/* 轮询收包：喂给 tcpip 线程 */
void ethernetif_poll(struct netif *netif)
{
    struct pbuf *p;
    while ((p = low_level_input(netif)) != NULL) {
        if (netif->input(p, netif) != ERR_OK) pbuf_free(p);
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
    s_txcfg.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
    s_txcfg.CRCPadCtrl = ETH_CRC_PAD_INSERT;

    MEMCPY(netif->hwaddr, s_mac, 6);
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

    /* PHY 复位 + 使能自协商（官方模板在 low_level_init 里做） */
    lan8720_init();

    /* 注意：HAL_ETH_Start() 不在此调用 —— 等 PHY 链路建立后由
       lan8720_poll() 调用（官方模板行为，避免链路未就绪时启动 DMA） */
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
