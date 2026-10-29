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
/* 一阶 Σ-Δ 抖动累加器（Q16）：把分数占空比分摊到相邻计数码之间，等效提升 PWM 分辨率，
 * 消除 1~4V 区间因 1 码台阶造成的量化极限环。关键：抖动步长 = 1 码 × VBUS，须远小于
 * CV 死区(5mV)才不会被反馈环当成误差去"掰"占空比→否则引发低频方波极限环。故 f_sw 须
 * 足够低（见 smu_ctrl_pick_arr：L=300µH + SMU_DI_RIPPLE_MAX 使 1 码≤~4mV < 5mV 死区）。 */
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
    /* 导通时间上限对应的"最高频率"：on-time=(vout/VBUS)/f ≥ 250ns */
    float f_ontime_max = (vout / SMU_VBUS) / 250e-9f;
    float L = 300e-6f;                        /* 实装 300µH/5A 输出电感（原 10µH/1.5A）；决定 f_sw 选择 */
    float isat = 1.30f;                       /* 原 10µH/1.5A 取值，对 5A 电感保守，不影响 f_sw 下限 */
    float denom = L * (isat - iout) * 2.0f;
    /* 频率下限取两个约束中较高者（f 越高 ΔI 越小）：
     * 1) 磁饱和：ΔI = vout*(1-vout/VBUS)/(L*f) ≤ 2*(isat-iout)
     * 2) 输出纹波：ΔI ≤ SMU_DI_RIPPLE_MAX —— 电感纹波经输出电容 ESR 直接
     *    变成输出纹波，轻载低压时若取最低频率 ΔI 可达安培级，纹波爆炸 */
    float f_min = 1e9f;
    if (denom > 1e-9f)
        f_min = vout * (1.0f - vout / SMU_VBUS) / denom;
    float f_ripple = vout * (1.0f - vout / SMU_VBUS) / (L * SMU_DI_RIPPLE_MAX);
    if (f_ripple > f_min) f_min = f_ripple;
    float f = f_min;                          /* 满足纹波约束的最低频率 */
    if (f > f_ontime_max) f = f_ontime_max;   /* 但不超导通时间上限 */
    f = ((uint32_t)(f / 10e3f + 0.5f)) * 10e3f; /* 量化到 10kHz 档：使 1 码步长随 f_sw 平滑，避免落入>5mV 死区带引发方波 */
    if (f < 20e3f) f = 20e3f;                 /* 频率下限：兼顾超低电压导通时间 */
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

/* 模式提交/切换时复位控制状态：避免旧积分或抖动累加器带入新工作点，
 * 并重新触发软启动斜坡（防止电压阶跃）。下一拍 CtrlTick 会立即重算占空比。 */
void SMU_CtrlReset(void)
{
    s_integ = 0.0f;
    s_dither_acc = 0U;
    s_softstart_ms = 0;
}

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
    /* 注意：保护锁存时【只】清零占空比/积分/软启动，保留 OUT_ACTIVE 与 EN。
     * 这样锁存期间输出被低边管钳在 0V（安全，与 OFF 接地一致），且 *CLS 清除
     * 锁存后控制环能自动恢复先前模式。
     * 若在此清 OUT_ACTIVE 或拉 EN=0：① 输出浮空→J4 现 0.56V 漏电；
     * ② *CLS 后 CtrlTick 因 OUT_ACTIVE=0 一直走清零分支、CC/CV 永不出力，
     *    且 MODE CC 因"模式未变"被短路不重跑序列 → 输出永久卡死。 */
}

void SMU_CtrlTick(void)
{
    /* 输出未建立或保护锁存 → 占空比清零 */
    if (!(g_smu.stat & SMU_ST_OUT_ACTIVE) || (g_smu.stat & SMU_ST_PROT)) {
        SMU_CtrlSetDuty(0.0f);
        s_softstart_ms = 0;
        s_integ = 0.0f;
        g_smu.stat &= (uint16_t)~SMU_ST_PROT_ARMED;   /* 输出未建立：解除保护武装 */
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
         * J4 上存在外部电压时（vmeas>vset）积分把 duty 压到 0、输出永远起不来。
         * 电压死区：误差在 ±SMU_CTRL_DEAD_V 内冻结积分（前馈仍维持输出），
         * 直接掐断反馈极限环（方波纹波）；静态误差≈死区量级，可忽略。 */
        float err = g_smu.vset - g_smu.vmeas;
        if (err > -SMU_CTRL_DEAD_V && err < SMU_CTRL_DEAD_V) {
            err = 0.0f;
        }
        /* 软启动阶段冻结积分(anti-windup)：ramp 时输出低于设定→err>0 会把积分一路
         * 攒到 INT_MAX，ramp 结束后需数秒回卷（实测 5.5V→5V 约 3s）。冻结后输出只由
         * 前馈+比例建立，ramp 结束时积分=0、输出即落在设定值附近，过冲与长尾同时消除。 */
        if (s_softstart_ms >= SMU_SOFTSTART_MS) {
            s_integ += SMU_CTRL_KI_CV * err;
            if (s_integ >  SMU_CTRL_INT_MAX_CV) s_integ =  SMU_CTRL_INT_MAX_CV;
            if (s_integ < -SMU_CTRL_INT_MAX_CV) s_integ = -SMU_CTRL_INT_MAX_CV;
        } else {
            s_integ = 0.0f;
        }
        target = g_smu.vset / SMU_VBUS
               + SMU_CTRL_KP_CV * err + s_integ;
        if (target > SMU_DUTY_MAX) target = SMU_DUTY_MAX;
        if (target < 0.0f) target = 0.0f;
    } else if (g_smu.mode == SMU_MODE_CC) {
        /* CC：纯 PI（无前馈，占空比主要由积分项建立）。死区防极限环。
         * 与 CV 区别：CV 有前馈(vset/VBUS)只需小积分修正→INT_MAX 小；
         * CC 无前馈，积分须能覆盖 0..DUTY_MAX 整个占空比区间，否则大负载
         * 到不了设定电流。曾误用增量式(target=s_duty+...)导致积分重复累加，
         * 现改为绝对式 PI。 */
        float err = g_smu.iset - g_smu.imeas;
        if (err > -SMU_CTRL_DEAD_MA && err < SMU_CTRL_DEAD_MA) {
            err = 0.0f;                              /* 死区 */
        }
        s_integ += SMU_CTRL_KI_CC * err;
        if (s_integ >  SMU_DUTY_MAX) s_integ =  SMU_DUTY_MAX;
        if (s_integ < 0.0f)         s_integ =  0.0f;
        target = SMU_CTRL_KP_CC * err + s_integ;
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
    } else {
        g_smu.stat |= SMU_ST_PROT_ARMED;             /* 软启动完成：武装 OVP/OCP 保护 */
    }
    SMU_CtrlSetDuty(target);
}
