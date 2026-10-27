/**
 * @file   smu_prot.c
 * @brief  保护逻辑：过压 / 过流 / ADC 饱和，锁存语义（PRD §7 SR-2/3/5）
 * @note   触发后 100ms 内安全关断（VOUT_EN=0 + 占空比清 0），需 *CLS 解除。
 */
#include "smu_prot.h"
#include "smu_board.h"
#include "smu_ctrl.h"
#include "smu_types.h"
#include <string.h>

extern smu_state_t g_smu;

/* ADC 输入绝对上限（ADS1115 数据手册：VDD+0.3V），超过按饱和/过流处理 */
#define SMU_ADC_SAT_CODE   (int16_t)(3.3f / SMU_ADC_LSB)

void SMU_ProtInit(void)
{
    /* 阈值由命令层/NV 提供，这里只清锁存 */
    g_smu.stat &= (uint16_t)~SMU_ST_PROT;
    g_smu.err = SMU_ERR_NONE;
}

static void prot_latch(int16_t err)
{
    g_smu.stat |= SMU_ST_PROT;
    g_smu.err = err;
    SMU_CtrlShutdown();
}

void SMU_ProtCheck(void)
{
    if (g_smu.stat & SMU_ST_PROT) return;              /* 已锁存 */

    if (g_smu.vmeas > g_smu.vprot) { prot_latch(SMU_ERR_OVP); return; }
    if (g_smu.imeas > g_smu.iprot) { prot_latch(SMU_ERR_OCP); return; }
    if (g_smu.adc[SMU_CH_CURR] > SMU_ADC_SAT_CODE) {   /* 电流通道顶到 ADC 保护二极管 */
        prot_latch(SMU_ERR_OCP);
        return;
    }
}

int SMU_ProtClear(void)
{
    if (!(g_smu.stat & SMU_ST_PROT)) return -1;
    g_smu.stat &= (uint16_t)~SMU_ST_PROT;
    g_smu.err = SMU_ERR_NONE;
    return 0;
}
