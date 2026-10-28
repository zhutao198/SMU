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
/* 一阶 Σ-Δ 抖动累加器（Q16）：把分数占空比分摊到相邻计数码之间，
 * 等效提升 PWM 分辨率，消除 1~4V 区间因 1 码≈162mV 台阶造成的极限环抖动。
 * 控制环 250Hz 切换 CCR，输出 LC 与 ADS1115 8 点均值把抖动平均成稳定小纹波。 */
static uint32_t s_dither_acc;
static uint32_t s_arr;   /* 当前 TIM2 自动重装载(决定 f_sw)，供自适应频率使用 */

/* 自适应开关频率：低电压时降低 f_sw 以延长导通时间(改善 1~2V 调节并提升分辨率)，
 * 高电压/大电流时维持高 f_sw 以免 10µH/1.5A 电感纹波饱和。
 * 占空比以分数表示，改 f_sw(=改 ARR) 后平均占空比不变 → 输出电压不跳变，仅纹波/分辨率变。
 *   on-time = (vout/VBUS)*(1/f)，要求 >= ~250ns(可靠调节下限)
 *   ΔI      = vout*(1-vout/VBUS)/(L*f)，峰值 = iout+ΔI/2 须 < 饱和限值 */
static uint32_t smu_ctrl_pick_arr(float vout, float iout)
{
    if (vout < 0.0f) vout = 0.0f;
    if (iout < 0.0f) iout = 0.0f;
    float f_ontime = (vout / SMU_VBUS) / 250e-9f;
    float L = 10e-6f;
    float isat = 1.30f;                       /* 10µH/1.5A 电感，留裕量 */
    float denom = L * (isat - iout) * 2.0f;
    float f_ripple = 1e9f;
    if (denom > 1e-9f)
        f_ripple = vout * (1.0f - vout / SMU_VBUS) / denom;
    float f = f_ontime;
    if (f_ripple > f) f = f_ripple;           /* 纹波更严格时取更高频率 */
    f = ((uint32_t)(f / 50e3f + 0.5f)) * 50e3f; /* 量化到 50kHz 档，减少抖动 */
    if (f < 100e3f) f = 100e3f;
    if (f > 400e3f) f = 400e3f;
    return (uint32_t)(SMU_TIM_CLK / f) - 1U;
}

void SMU_CtrlInit(void)
{
    s_duty = 0.0f;
    s_integ = 0.0f;
    s_softstart_ms = 0;
    s_dither_acc = 0U;
    g_smu.duty = 0.0f;
    g_smu.integ = 0.0f;
    SMU_CtrlSetDuty(0.0f);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    s_arr = __HAL_TIM_GET_AUTORELOAD(&htim2);
    /* 关断态(上电/outp off)保持低边管导通：EN=1 + duty=0 → 输出被钳在 0V。
     * 否则 EN=0 使两管都关，J4 浮空被功率级漏电流(LM5106 自举残压使高边微导通
     * 或 35V 经关断管漏电)抬到 ~0.56V。VM/IM 测量模式由 smu_mode 单独置 EN=0 真高阻。 */
    HAL_GPIO_WritePin(SMU_EN_PORT, SMU_EN_PIN, GPIO_PIN_SET);
}

void SMU_CtrlSetDuty(float d)
{
    if (d < 0.0f) d = 0.0f;
    if (d > SMU_DUTY_MAX) d = SMU_DUTY_MAX;
    s_duty = d;
    g_smu.duty = d;
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim2);
    float fcounts = d * (float)(arr + 1U);          /* 期望计数（可含小数） */
    uint32_t base = (uint32_t)fcounts;              /* 向下取整 */
    uint32_t frac = (uint32_t)((fcounts - (float)base) * 65536.0f);
    s_dither_acc += frac;
    if (s_dither_acc >= 65536U) {                   /* 本周期多用 1 个计数 */
        s_dither_acc -= 65536U;
        base += 1U;
    }
    if (base > arr) base = arr;                     /* 近满占空比时夹紧 */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, base);
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
    p = smu_cat(p, ",f=");
    p = smu_cat_uint(p, (unsigned long)(SMU_TIM_CLK / (arr + 1U)));
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

    /* 自适应开关频率：依 vout/iout 调整 TIM2 ARR。
     * 占空比以分数表示，改 ARR 后平均占空比不变 → 输出电压不跳变。
     * 电压取 max(vset,vmeas)：重负载开机瞬间 vmeas 未起来也能维持高 f_sw，
     * 避免低频下电感纹波饱和；稳态调节时仍按实际电压降频以改善 1~2V。 */
    float v = (g_smu.vset > g_smu.vmeas) ? g_smu.vset : g_smu.vmeas;
    uint32_t new_arr = smu_ctrl_pick_arr(v, g_smu.imeas);
    if (new_arr != s_arr) {
        __HAL_TIM_SET_AUTORELOAD(&htim2, new_arr);
        s_arr = new_arr;
        s_dither_acc = 0U;                 /* 重新对齐抖动累加器 */
        SMU_CtrlSetDuty(s_duty);           /* 用新 ARR 立即重算 CCR2，避免瞬时越界 */
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
