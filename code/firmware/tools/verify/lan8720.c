/**
 * @file   lan8720.c
 * @brief  LAN8720 PHY 驱动（精简版，PHY 地址默认 0 —— 设计文档 §10.1 O-3 待实测）
 * @note   CubeMX 工程会生成自己的 lan8720.c，此文件仅供独立构建/验证。
 */
#include "main.h"
#include "lwip/netif.h"

extern ETH_HandleTypeDef heth;

#define LAN8720_PHY_ADDR   0x00U

/* 基本寄存器 */
#define PHY_BCR            0x00U   /* 基本控制 */
#define PHY_BSR            0x01U   /* 基本状态 */
#define PHY_SPEC_STATUS    0x1FU   /* 厂商状态 */

#define PHY_BCR_RESET      0x8000U
#define PHY_BCR_AUTONEG    0x1000U
#define PHY_BCR_RESTART_AN 0x0200U

#define PHY_BSR_LINK       0x0004U

static uint32_t lan8720_read(uint32_t reg)
{
    uint32_t val = 0;
    HAL_ETH_ReadPHYRegister(&heth, LAN8720_PHY_ADDR, reg, &val);
    return val;
}

static void lan8720_write(uint32_t reg, uint32_t val)
{
    HAL_ETH_WritePHYRegister(&heth, LAN8720_PHY_ADDR, reg, val);
}

void lan8720_init(void)
{
    uint32_t bsr;

    lan8720_write(PHY_BCR, PHY_BCR_RESET);
    HAL_Delay(50);

    bsr = lan8720_read(PHY_BSR);
    if ((bsr & PHY_BSR_LINK) == 0) {
        /* 无链接也继续（自协商仍可建立） */
    }

    /* 使能自协商并重启 */
    lan8720_write(PHY_BCR, PHY_BCR_AUTONEG | PHY_BCR_RESTART_AN);
    HAL_Delay(100);
}

/* 轮询链接状态并同步 MAC 配置 */
void lan8720_poll(struct netif *netif)
{
    uint32_t bsr = lan8720_read(PHY_BSR);
    if ((bsr & PHY_BSR_LINK) != 0 && !netif_is_link_up(netif)) {
        ETH_MACConfigTypeDef mac;
        HAL_ETH_GetMACConfig(&heth, &mac);
        /* 10/100 + full duplex（自协商结果简化为全双工） */
        mac.DuplexMode = ETH_FULLDUPLEX_MODE;
        mac.Speed = ETH_SPEED_100M;
        HAL_ETH_SetMACConfig(&heth, &mac);
        netif_set_link_up(netif);
    } else if ((bsr & PHY_BSR_LINK) == 0 && netif_is_link_up(netif)) {
        netif_set_link_down(netif);
    }
}
