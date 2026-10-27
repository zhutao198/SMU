/**
 * @file   smu_ctrl.c
 * @brief  输出执行：PWM 占空比 + GPIO 通断 + CV 开环 / CC 比例环（PRD §4.2）
 * @note   M1 为开环为主（里程碑定义）：CV 用前馈 duty=vset/35；
 *         CC 无前馈可用，采用比例环（M2 换 PI 并整定参数）。
 */
#include "smu_ctrl.h"
#include "smu_board.h"
#include "smu_mode.h"
#include "smu_types.h"
#include "smu_fmt.h"
#include <string.h>

extern smu_state_t g_smu;
extern TIM_HandleTypeDef htim2;

static float s_duty;
static float s_integ;
static uint32_t s_softstart_ms;

void SMU_CtrlInit(void)
{
    s_duty = 0.0f;
    s_integ = 0.0f;
    s_softstart_ms = 0;
    g_smu.duty = 0.0f;
    g_smu.integ = 0.0f;
    SMU_CtrlSetDuty(0.0f);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
}

void SMU_CtrlSetDuty(float d)
{
    if (d < 0.0f) d = 0.0f;
    if (d > SMU_DUTY_MAX) d = SMU_DUTY_MAX;
    s_duty = d;
    g_smu.duty = d;
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim2);
    uint32_t ccr = (uint32_t)(d * (float)(arr + 1U));
    if (ccr > arr) ccr = arr;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, ccr);
}

float SMU_CtrlGetDuty(void) { return s_duty; }

/* 硬件状态回显（诊断：确认 PWM/GPIO 是否真的被驱动） */
void SMU_CtrlDbgInfo(char *buf, uint32_t len)
{
    char *p = buf;
    uint32_t ccr = __HAL_TIM_GET_COMPARE(&htim2, TIM_CHANNEL_2);
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim2);
    p = smu_cat(p, "ccr2=");
    p = smu_cat_uint(p, (unsigned long)ccr);
    p = smu_cat(p, ",arr=");
    p = smu_cat_uint(p, (unsigned long)arr);
    p = smu_cat(p, ",en=");
    p = smu_cat_uint(p, (unsigned long)(HAL_GPIO_ReadPin(SMU_EN_PORT, SMU_EN_PIN) == GPIO_PIN_SET));
    p = smu_cat(p, ",itest=");
    p = smu_cat_uint(p, (unsigned long)(HAL_GPIO_ReadPin(SMU_ITEST_PORT, SMU_ITEST_PIN) == GPIO_PIN_SET));
    p = smu_cat(p, ",ivtest=");
    p = smu_cat_uint(p, (unsigned long)(HAL_GPIO_ReadPin(SMU_IVTEST_PORT, SMU_IVTEST_PIN) == GPIO_PIN_SET));
    *p = '\0';
    (void)len;
}

void SMU_CtrlShutdown(void)
{
    SMU_CtrlSetDuty(0.0f);
    s_softstart_ms = 0;
    s_integ = 0.0f;
    g_smu.outp = 0;
    g_smu.stat &= (uint16_t)~SMU_ST_OUT_ACTIVE;
    HAL_GPIO_WritePin(SMU_EN_PORT, SMU_EN_PIN, GPIO_PIN_RESET);
}

void SMU_CtrlTick(void)
{
    /* 输出未建立或保护锁存 → 占空比清零 */
    if (!(g_smu.stat & SMU_ST_OUT_ACTIVE) || (g_smu.stat & SMU_ST_PROT)) {
        SMU_CtrlSetDuty(0.0f);
        s_softstart_ms = 0;
        s_integ = 0.0f;
        return;
    }

    /* 软启动斜坡（PRD FR-OUT-5） */
    float target;
    if (g_smu.mode == SMU_MODE_CV) {
        /* CV：前馈(vset/VBUS) + PI 修正。前馈给工作点，PI 消静差与负载变化。
         * 曾用纯开环：2kΩ 负载下 5V 设定实测 4.86V（误差 2.8%）。
         * 注意：积分限幅必须远小于前馈量（0.143）！曾用 ±0.15 导致
         * J4 上存在外部电压时（vmeas>vset）积分把 duty 压到 0、输出永远起不来。 */
        float err = g_smu.vset - g_smu.vmeas;
        s_integ += SMU_CTRL_KI_CV * err;
        if (s_integ >  SMU_CTRL_INT_MAX_CV) s_integ =  SMU_CTRL_INT_MAX_CV;
        if (s_integ < -SMU_CTRL_INT_MAX_CV) s_integ = -SMU_CTRL_INT_MAX_CV;
        target = g_smu.vset / SMU_VBUS
               + SMU_CTRL_KP_CV * err + s_integ;
        if (target > SMU_DUTY_MAX) target = SMU_DUTY_MAX;
        if (target < 0.0f) target = 0.0f;
    } else if (g_smu.mode == SMU_MODE_CC) {
        /* CC：比例 + 积分（无前馈可用）。加死区防抖动；
         * 曾用 kp=0.01/mA：1mA 误差→1% duty→电压跳变→震荡。 */
        float err = g_smu.iset - g_smu.imeas;
        if (err > -SMU_CTRL_DEAD_MA && err < SMU_CTRL_DEAD_MA) {
            err = 0.0f;                              /* 死区 */
        }
        s_integ += SMU_CTRL_KI_CC * err;
        if (s_integ >  SMU_CTRL_INT_MAX_CC) s_integ =  SMU_CTRL_INT_MAX_CC;
        if (s_integ < -SMU_CTRL_INT_MAX_CC) s_integ = -SMU_CTRL_INT_MAX_CC;
        target = s_duty + SMU_CTRL_KP_CC * err + s_integ;
        if (target > SMU_DUTY_MAX) target = SMU_DUTY_MAX;
        if (target < 0.0f) target = 0.0f;
    } else {
        target = 0.0f;
    }

    if (s_softstart_ms < SMU_SOFTSTART_MS) {
        s_softstart_ms += (1000U / SMU_CTRL_HZ);
        float ramp = (float)s_softstart_ms / (float)SMU_SOFTSTART_MS;
        if (ramp > 1.0f) ramp = 1.0f;
        target *= ramp;
    }
    SMU_CtrlSetDuty(target);
}
