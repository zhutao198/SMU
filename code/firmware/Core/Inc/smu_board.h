/**
 * @file   smu_board.h
 * @brief  SMU 源表 —— 板级配置（引脚 / 量程 / 系数 / 时序）
 * @note   所有数值均来自《硬件原理分析与固件设计.md》（已用 PADS 网表逐网核实），
 *         修改任何常量请同步更新该文档与 PRD 对应章节。
 */
#ifndef SMU_BOARD_H
#define SMU_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* ==================== 控制引脚（网表核实，设计文档 §6） ==================== */
/* VOUT_PWM : PB3  TIM2_CH2（需 AFIO 部分重映射）→ LM5106 IN  */
#define SMU_PWM_PORT        GPIOB
#define SMU_PWM_PIN         GPIO_PIN_3
/* VOUT_EN  : PB4  → LM5106 EN（高有效，使能整个降压级）      */
#define SMU_EN_PORT         GPIOB
#define SMU_EN_PIN          GPIO_PIN_4
/* I_TEST_EN: PD1  → Q5 栅极（电流测量：把 I_T_P 接到地）      */
#define SMU_ITEST_PORT      GPIOD
#define SMU_ITEST_PIN       GPIO_PIN_1
/* I_V_TEST_EN: PD2 → Q6 栅极（0=输出接通，1=输出断开）        */
#define SMU_IVTEST_PORT     GPIOD
#define SMU_IVTEST_PIN      GPIO_PIN_2
/* RDY_1    : PB5  ← ADS1115 ALERT/RDY（转换完成，低有效）     */
#define SMU_RDY_PORT        GPIOB
#define SMU_RDY_PIN         GPIO_PIN_5
/* J18 拨码开关（公共端接地，内部上拉，0=闭合）                */
#define SMU_J18_1_PORT      GPIOC
#define SMU_J18_1_PIN       GPIO_PIN_11
#define SMU_J18_2_PORT      GPIOC
#define SMU_J18_2_PIN       GPIO_PIN_12

/* ==================== 外设句柄（CubeMX 生成，见 README 清单） ============== */
extern TIM_HandleTypeDef  htim2;      /* TIM2_CH2 : 输出 PWM            */
extern I2C_HandleTypeDef  hi2c1;      /* I2C1     : ADS1115             */
extern UART_HandleTypeDef huart1;     /* USART1   : CH340N USB 串口     */
extern IWDG_HandleTypeDef hiwdg;      /* IWDG     : 100ms 看门狗        */

/* ==================== ADS1115（I2C 地址 0x48，网表核实） ================== */
#define SMU_ADS1115_ADDR        (0x48U << 1)   /* HAL 8 位地址 */
#define ADS1115_REG_CONV        0x00U
#define ADS1115_REG_CONFIG      0x01U
#define ADS1115_REG_LO_THRESH   0x02U
#define ADS1115_REG_HI_THRESH   0x03U

/* ADS1115 MUX 编码（网表 + 用户确认：**差分测量**，非单端）
 * 电压通道：AIN0(V_TEST_AD) − AIN1(VREF 1.65V)
 * 电流通道：AIN2(I_V_AD)    − AIN3(VREF 1.65V，同源)
 * 以 VREF 为参考做比率测量：共模抑制 3.3V 漂移与 PWM 耦合噪声 */
#define ADS_MUX_VOLT           0x0000U   /* MUX=000: AIN0 − AIN1 */
#define ADS_MUX_CURR           0x3000U   /* MUX=011: AIN2 − AIN3 */
/* 配置字：单次转换 + PGA + 860SPS + 关闭比较器
 * 注：PGA 编码 010 = ±2.048V（LSB=125µV）。2026-10-27 实测：写 001(0x0200,
 * 标称 ±4.096V) 时器件实际按 ±2.048V 工作，故显式用 010 与实测一致。
 * 差分信号 ±1.7V 内（V_TEST_AD 0~3.11V 对 VREF 1.643V），±2.048V 余量充足。 */
#define ADS_CFG_PGA_2048        0x0400U   /* PGA[2:0]=010 → ±2.048V，LSB=62.5µV */
/* bit8=MODE(1=单次) + DR=111(860SPS) + bit1:0=11。
 * 曾漏掉 MODE 位（0x0100）导致器件在连续转换模式工作：OS 位行为异常、
 * 通道切换与轮询/校验逻辑失效（电压/电流读数串味）。
 * 注：COMP_QUE(bits3:2) 实为 00（传统比较器），并非注释原先写的 11。
 * 这也解释了 RDY 引脚为何一直不拉低：**ALERT/RDY 脚只有在 COMP_QUE=11
 * 时才是"转换完成"输出**，本配置下它是比较器 ALERT 输出，且板上无上拉。
 * 当前改用"写 config → 固定延时 2ms → 读 conv"，不依赖该脚。 */
#define ADS_CFG_MODE_DR         0x01E3U   /* MODE=1(单次), DR=111(860SPS), COMP_QUE=00 */
#define ADS1115_CFG_VOLT        (0x8000U | ADS_MUX_VOLT | ADS_CFG_PGA_2048 | ADS_CFG_MODE_DR)
#define ADS1115_CFG_CURR        (0x8000U | ADS_MUX_CURR | ADS_CFG_PGA_2048 | ADS_CFG_MODE_DR)

/* ADC 逻辑通道（差分对） */
#define SMU_CH_VOLT             0U   /* AIN0 − AIN1：电压 */
#define SMU_CH_CURR             1U   /* AIN2 − AIN3：电流 */
#define SMU_ADC_CH_NUM          2U

/* ==================== 量程与换算系数（PRD §5 / §7） ======================= */
#define SMU_V_MAX               30.0f    /* 电压设定上限 V          */
#define SMU_I_MAX_MA            30.0f    /* 电流设定上限 mA         */
#define SMU_V_PROT_MIN          5.0f     /* 过压保护下限 V          */
#define SMU_V_PROT_DEF          30.0f    /* 过压保护默认 V          */
#define SMU_I_PROT_MIN          1.0f     /* 过流保护下限 mA         */
#define SMU_I_PROT_DEF          30.0f    /* 过流保护默认 mA         */
#define SMU_VDIV_RATIO          9.66f    /* R137 86.6k / R157 10k   */
#define SMU_INA_GAIN            50.4f    /* INA129: 1 + 49.4k/R50(1k) */
#define SMU_VREF_NOM            1.65f    /* U18-B 缓冲分压          */
#define SMU_RSENSE              1.0f     /* R133 采样电阻 1Ω        */
#define SMU_VBUS                35.0f    /* 升压母线 V              */
#define SMU_ADC_FS              2.048f   /* ADS1115 PGA ±2.048V（单侧满量程） */
/* LSB = FSR/32768 = 62.5µV（2026-10-27 实测反推：val=-22786 ↔ U18-A=0.218V）
 * 注：曾误记为 125µV（那是 2×FSR/32768），导致 V 读数虚高约 2 倍 */
#define SMU_ADC_LSB             (SMU_ADC_FS / 32768.0f)      /* 62.5uV/LSB */
#define SMU_DUTY_MAX            0.95f    /* 自举驱动占空比上限      */

/* ---- 差分测量换算（见文件头说明）----
 * 电压：J4 = (Vcode*LSB + VREF) * VDIV_RATIO + b
 * 电流：I_V_AD = VREF + I[A]*RSENSE*INA_GAIN
 *       差分码 Icode = (I_V_AD - VREF)/LSB = I*RSENSE*GAIN/LSB
 *       → I[mA] = Icode * LSB * 1000 / (RSENSE*INA_GAIN) + b   （VREF 被差分抵消）*/
#define SMU_CAL_V_K_DEFAULT     (SMU_ADC_LSB * SMU_VDIV_RATIO)                    /* 0.000604 V/码 */
#define SMU_CAL_V_B_DEFAULT     (SMU_VREF_NOM * SMU_VDIV_RATIO)                   /* 15.874   V    */
#define SMU_CAL_I_K_DEFAULT     (SMU_ADC_LSB * 1000.0f / (SMU_RSENSE * SMU_INA_GAIN)) /* 1.240 mA/码 */
#define SMU_CAL_I_B_DEFAULT     0.0f

/* ==================== 时序参数（PRD §7 SR-4） ============================= */
#define SMU_SEQ_TICK_MS         10U      /* 模式切换序列节拍         */
#define SMU_SEQ_SETTLE_MS       20U      /* 输出/测量切换稳定时间    */
#define SMU_CTRL_HZ             250U     /* 控制环频率               */
#define SMU_TIM_CLK             84000000UL /* TIM2 时钟(APB1=42MHz×2=84MHz) */
/* ---- PI 控制参数（M2 整定，250Hz 控制环）----
 * CV：前馈(vset/35≈0.143@5V) + PI 修正。
 *      KP=0.002（1V 误差→0.2% duty），KI=0.0002/tick，**积分限幅 ±0.05**
 *      （必须远小于前馈量，否则外部电压倒灌时积分会把 duty 压到 0 无法起输）
 * CC：比例 + 积分 + 死区。KP=0.004/mA，KI=0.002/tick，积分限幅 ±0.05，死区 0.05mA */
#define SMU_CTRL_KP_CV          0.002f
#define SMU_CTRL_KI_CV          0.0002f
#define SMU_CTRL_INT_MAX_CV     0.05f
#define SMU_CTRL_KP_CC          0.004f
#define SMU_CTRL_KI_CC          0.002f
#define SMU_CTRL_INT_MAX_CC     0.05f
#define SMU_CTRL_DEAD_MA        0.05f
#define SMU_MEAS_FILTER_LEN     8U       /* 滑动平均窗口             */
#define SMU_SOFTSTART_MS        300U     /* 0→设定值 软启动时间      */
#define SMU_IWDG_TIMEOUT_MS     100U     /* 看门狗超时               */

#ifdef __cplusplus
}
#endif

#endif /* SMU_BOARD_H */
