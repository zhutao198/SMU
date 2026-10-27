/**
 * @file   smu_mode.c
 * @brief  模式状态机 + 安全切换时序（PRD §4.1 FR-MODE-6 / §7 SR-4）
 * @note   时序（硬件真值表推导）：
 *         离开输出：VOUT_EN=0 → 20ms → I_V_TEST_EN=1 → I_TEST_EN(按模式)
 *         进入输出：I_V_TEST_EN=0 → 20ms → VOUT_EN=1（占空比由控制环软启动）
 */
#include "smu_mode.h"
#include "smu_board.h"
#include "smu_ctrl.h"
#include "smu_types.h"
#include <string.h>

extern smu_state_t g_smu;

typedef enum {
    SEQ_IDLE = 0,
    SEQ_S1,      /* 已关 VOUT_EN，等待稳定 */
    SEQ_S2,      /* 已置 I_V_TEST_EN / I_TEST_EN */
    SEQ_S3       /* 置 VOUT_EN=1，完成 */
} seq_step_t;

static struct {
    seq_step_t step;
    smu_mode_t target;
    uint16_t   timer_ms;
    uint8_t    active;
} s_seq;

static void hw_write(uint8_t en, uint8_t itest, uint8_t ivtest)
{
    HAL_GPIO_WritePin(SMU_EN_PORT,    SMU_EN_PIN,    en     ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SMU_ITEST_PORT, SMU_ITEST_PIN, itest  ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SMU_IVTEST_PORT, SMU_IVTEST_PIN, ivtest ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void SMU_ModeInit(void)
{
    memset(&s_seq, 0, sizeof(s_seq));
    g_smu.mode = SMU_MODE_HIZ;
    g_smu.outp = 0;
    /* 保护阈值默认值：必须在任何保护检查之前设置，
     * 否则 vprot/iprot 为 0 会导致上电即误触发保护（PROT 锁存）。 */
    g_smu.vprot = SMU_V_PROT_DEF;      /* 30V */
    g_smu.iprot = SMU_I_PROT_DEF;      /* 30mA */
    g_smu.vset  = 0.0f;
    g_smu.iset  = 0.0f;
    SMU_CtrlSetDuty(0.0f);
    hw_write(0, 0, 0);              /* 上电安全态：EN=0, I_TEST=0, I_V_TEST=0 → HIZ（SR-1） */
}

int SMU_ModeBusy(void) { return s_seq.active; }

int SMU_ModeRequest(smu_mode_t m)
{
    if (s_seq.active) return SMU_ERR_MODE_BUSY;
    if (m == g_smu.mode) return 0;
    s_seq.target = m;
    s_seq.step = SEQ_S1;
    s_seq.timer_ms = 0;
    s_seq.active = 1;
    SMU_CtrlSetDuty(0.0f);
    hw_write(0, 0, HAL_GPIO_ReadPin(SMU_IVTEST_PORT, SMU_IVTEST_PIN) == GPIO_PIN_SET);
    return 0;
}

void SMU_ModeTick(void)
{
    if (!s_seq.active) return;
    s_seq.timer_ms += SMU_SEQ_TICK_MS;

    switch (s_seq.step) {
    case SEQ_S1:
        if (s_seq.timer_ms >= SMU_SEQ_SETTLE_MS) {
            /* 置测量/断开侧 */
            uint8_t itest  = (s_seq.target == SMU_MODE_IM) ? 1U : 0U;
            uint8_t ivtest = (s_seq.target == SMU_MODE_VM || s_seq.target == SMU_MODE_IM) ? 1U : 0U;
            hw_write(0, itest, ivtest);
            s_seq.step = SEQ_S2;
            s_seq.timer_ms = 0;
        }
        break;
    case SEQ_S2:
        if (s_seq.timer_ms >= SMU_SEQ_SETTLE_MS) {
            if (s_seq.target == SMU_MODE_CV || s_seq.target == SMU_MODE_CC) {
                hw_write(1, 0, 0);              /* 接通输出 */
                s_seq.step = SEQ_S3;
                s_seq.timer_ms = 0;
            } else {
                g_smu.mode = s_seq.target;      /* HIZ/VM/IM 完成 */
                g_smu.outp = 0;
                g_smu.stat &= (uint16_t)~SMU_ST_OUT_ACTIVE;
                s_seq.active = 0;
            }
        }
        break;
    case SEQ_S3:
        if (s_seq.timer_ms >= 5U) {             /* EN 已建立，交还控制环 */
            g_smu.mode = s_seq.target;
            g_smu.outp = (s_seq.target == SMU_MODE_CV || s_seq.target == SMU_MODE_CC) ? 1U : 0U;
            g_smu.stat |= SMU_ST_OUT_ACTIVE;
            s_seq.active = 0;
        }
        break;
    default:
        s_seq.active = 0;
        break;
    }
}
