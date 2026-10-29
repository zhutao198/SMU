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
    static uint8_t s_ovp_cnt = 0;
    static uint8_t s_ocp_cnt = 0;
    const uint8_t PROT_DEBOUNCE = 20U;   /* 连续 20 拍(≈80ms)超限才锁存，屏蔽尖峰 */

    if (g_smu.stat & SMU_ST_PROT) return;              /* 已锁存 */

    /* 软启动/输出建立期间不武装保护：使能瞬间电压/电流尖峰（RC 滤波使电流反馈变慢，
     * CC 软启动时实际电流会冲高、并经输出电容把电压也冲高）会瞬时触发 OVP/OCP，
     * 造成"每次输出前都要 *CLS"的现象。软启动完成(300ms)且输出已建立后才武装。 */
    if (!(g_smu.stat & SMU_ST_PROT_ARMED)) {
        s_ovp_cnt = 0;
        s_ocp_cnt = 0;
        return;
    }

    if (g_smu.vmeas > g_smu.vprot) {
        if (++s_ovp_cnt >= PROT_DEBOUNCE) { prot_latch(SMU_ERR_OVP); return; }
    } else {
        s_ovp_cnt = 0;
    }
    if (g_smu.imeas > g_smu.iprot) {
        if (++s_ocp_cnt >= PROT_DEBOUNCE) { prot_latch(SMU_ERR_OCP); return; }
    } else {
        s_ocp_cnt = 0;
    }
    /* 注：原"原始 ADC 码超饱和即锁存 OCP"已移除——buck 开关纹波会让 INA 输出
     * 瞬时削顶，原始码瞬间超 SMU_ADC_SAT_CODE，误触发 OCP 把输出钉在 0V。
     * 过流保护改由滤波后的 imeas>iprot 承担：真短路/过流时 imeas 均值同样
     * 超 iprot，不会漏保护；而纹波导致的瞬时削顶不再误锁。 */
}

int SMU_ProtClear(void)
{
    if (!(g_smu.stat & SMU_ST_PROT)) return -1;
    g_smu.stat &= (uint16_t)~SMU_ST_PROT;
    g_smu.err = SMU_ERR_NONE;
    return 0;
}
