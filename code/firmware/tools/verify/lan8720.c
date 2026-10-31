/**
 * @file   lan8720.c
 * @brief  LAN8720 PHY 驱动（精简版，PHY 地址默认 0 —— 设计文档 §10.1 O-3 待实测）
 * @note   CubeMX 工程会生成自己的 lan8720.c，此文件仅供独立构建/验证。
 */
#include "main.h"
#include "lwip/netif.h"

extern ETH_HandleTypeDef heth;

#define LAN8720_PHY_ADDR   0x00U   /* 默认地址；lan8720_init 会扫描实际 strap 地址覆盖 */

/* 基本寄存器 */
#define PHY_BCR            0x00U   /* 基本控制 */
#define PHY_BSR            0x01U   /* 基本状态 */
#define PHY_SPEC_STATUS    0x1FU   /* 厂商状态 */

#define PHY_BCR_RESET      0x8000U
#define PHY_BCR_AUTONEG    0x1000U
#define PHY_BCR_RESTART_AN 0x0200U

#define PHY_BSR_LINK       0x0004U

/* 调试观测（SWD 可读）：探测到的 PHY 地址、链路/启动状态。g_link_dbg: bit0=当前 link，bit7=已 HAL_ETH_Start */
uint8_t g_phy_addr_dbg = 0;
uint8_t g_link_dbg = 0;
uint16_t g_bsr_dbg = 0;   /* 原始 PHY BSR 值（SWD 可读），判断链路真实状态 */
uint8_t g_eth_evt = 0;    /* 0=start块未执行 2=HAL_ETH_Start OK 3=HAL_ETH_Start 失败 */

/* 实际 PHY 地址（auto-detect 后填充），避免 PRD R-10 写死 0 在 strap 非 0 时失效 */
static uint8_t s_phy_addr = LAN8720_PHY_ADDR;

static uint32_t lan8720_read(uint32_t reg)
{
    uint32_t val = 0;
    HAL_ETH_ReadPHYRegister(&heth, s_phy_addr, reg, &val);
    return val;
}

static void lan8720_write(uint32_t reg, uint32_t val)
{
    HAL_ETH_WritePHYRegister(&heth, s_phy_addr, reg, val);
}

void lan8720_init(void)
{
    uint32_t bsr;

    /* 自动探测 PHY 地址：PRD R-10 指出 strap 地址需实测，写死 0 在地址非 0 时
     * 会读不到链路。扫描 0..31 取首个返回有效 PHY ID（非 0/非全 1）的地址。 */
    s_phy_addr = LAN8720_PHY_ADDR;
    for (uint8_t a = 0; a < 32U; a++) {
        uint32_t id1 = 0, id2 = 0;
        if (HAL_ETH_ReadPHYRegister(&heth, a, 0x02U, &id1) == HAL_OK &&
            HAL_ETH_ReadPHYRegister(&heth, a, 0x03U, &id2) == HAL_OK) {
            if (id1 != 0U && id1 != 0xFFFFU && id2 != 0U && id2 != 0xFFFFU) {
                s_phy_addr = a;
                g_phy_addr_dbg = a;
                break;
            }
        }
        g_phy_addr_dbg = s_phy_addr;
    }

    lan8720_write(PHY_BCR, PHY_BCR_RESET);
    HAL_Delay(50);

    bsr = lan8720_read(PHY_BSR);
    if ((bsr & PHY_BSR_LINK) == 0) {
        /* 无链接也继续（自协商仍可建立） */
    }

    /* 使能自协商并重启 */
    lan8720_write(PHY_BCR, PHY_BCR_AUTONEG | PHY_BCR_RESTART_AN);
    HAL_Delay(100);

    /* 无条件启动 MAC/DMA：原设计在 lan8720_poll 中依据 BSR LINK 位触发启动，
     * 但实测该位判定不稳定（链路位偶发为 0，或被 netif_is_link_up 门控），导致
     * HAL_ETH_Start 永不执行、板子在 192.168.7.200 完全不可达。改为初始化阶段直接
     * 启动一次；链路就绪后由 lan8720_poll 维护 netif link 状态即可。 */
    ETH_MACConfigTypeDef mac;
    HAL_ETH_GetMACConfig(&heth, &mac);
    mac.DuplexMode = ETH_FULLDUPLEX_MODE;
    mac.Speed = ETH_SPEED_100M;
    HAL_ETH_SetMACConfig(&heth, &mac);
    HAL_StatusTypeDef st = HAL_ETH_Start(&heth);
    g_eth_evt = (st == HAL_OK) ? 2 : 3;
}

/* 轮询链接状态并同步 netif link 标志。
 * 注意：MAC/DMA 已在 lan8720_init 中无条件 HAL_ETH_Start 启动，此处只维护
 * 链路 UP/DOWN 状态（供 LwIP 决定是否收发），严禁再次调用 HAL_ETH_Start，
 * 否则会重复初始化 DMA 描述符导致 RX 异常。 */
void lan8720_poll(struct netif *netif)
{
    uint32_t bsr = lan8720_read(PHY_BSR);
    g_link_dbg = (bsr & PHY_BSR_LINK) ? 1 : 0;
    g_bsr_dbg = (uint16_t)bsr;
    if ((bsr & PHY_BSR_LINK) != 0) {
        if (!netif_is_link_up(netif)) {
            netif_set_link_up(netif);
            g_link_dbg |= 0x80;
        }
    } else {
        if (netif_is_link_up(netif)) {
            netif_set_link_down(netif);
        }
        g_link_dbg &= ~0x80;
    }
}
