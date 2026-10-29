/**
 * @file   smu_types.h
 * @brief  全局类型 / 状态结构 / 错误码（PRD §6 通信协议）
 * @note   状态字位域与错误码必须与 PRD §6.2、Web 页面 errText() 保持一致。
 */
#ifndef SMU_TYPES_H
#define SMU_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "smu_board.h"     /* SMU_ADC_CH_NUM 等板级常量 */

/* ==================== 工作模式（硬件真值表，设计文档 §3.3） ================ */
typedef enum {
    SMU_MODE_HIZ = 0,   /* 高阻：I_V_TEST_EN=0, VOUT_EN=0        */
    SMU_MODE_CV,        /* 恒压输出：I_V_TEST_EN=0, VOUT_EN=1    */
    SMU_MODE_CC,        /* 恒流输出：同上，闭环量改为电流        */
    SMU_MODE_VM,        /* 电压测量：I_V_TEST_EN=1, I_TEST_EN=0  */
    SMU_MODE_IM         /* 电流测量：I_V_TEST_EN=1, I_TEST_EN=1  */
} smu_mode_t;

/* ==================== 状态字位域（PRD §6.2） ============================= */
#define SMU_ST_READY        0x0001U   /* bit0 自检通过            */
#define SMU_ST_PROT         0x0002U   /* bit1 保护锁存            */
#define SMU_ST_ADC_OK       0x0004U   /* bit2 ADS1115 在线        */
#define SMU_ST_OUT_ACTIVE   0x0008U   /* bit3 输出已建立          */
#define SMU_ST_PROT_ARMED   0x0010U   /* bit4 保护已武装（软启动完成后才允许 OVP/OCP 锁存）*/

/* ==================== 错误码（PRD §6.2 / Web errText） =================== */
#define SMU_ERR_NONE        0
#define SMU_ERR_CMD         100    /* 命令错误            */
#define SMU_ERR_RANGE       110    /* 参数越界            */
#define SMU_ERR_MODE_BUSY   120    /* 模式切换被拒绝/忙   */
#define SMU_ERR_OVP         121    /* 过压保护锁存        */
#define SMU_ERR_OCP         122    /* 过流保护锁存        */
#define SMU_ERR_ADC         130    /* ADC 通信失败        */
#define SMU_ERR_SELFTEST    140    /* 自检失败            */
#define SMU_ERR_WDG         150    /* 看门狗复位          */

/* ==================== 标定系数（NFR-1：y = k*x + b） ===================== */
typedef struct {
    float k;
    float b;
} smu_cal_t;

/* ==================== 非易失配置（NFR-2：Flash 双备份） ================== */
#define SMU_NV_MAGIC        0x534D5531U   /* "SMU1" */

typedef struct {
    uint32_t magic;
    uint32_t write_cnt;          /* 写入计数，取较大者        */
    uint8_t  use_dhcp;
    uint8_t  ip[4];
    uint8_t  netmask[4];
    uint8_t  gw[4];
    uint16_t tcp_port;
    smu_cal_t cal_v;             /* 电压标定                  */
    smu_cal_t cal_i;             /* 电流标定                  */
    uint8_t  rsv[16];
    uint32_t crc32;              /* 以上字段的 CRC32          */
} smu_nv_t;

/* ==================== 全局运行状态 ======================================== */
typedef struct {
    smu_mode_t mode;
    float      vset;             /* 电压设定 V                */
    float      iset;             /* 电流设定 mA               */
    float      vprot;            /* 过压阈值 V                */
    float      iprot;            /* 过流阈值 mA               */
    uint8_t    outp;             /* 输出开关 0/1              */
    /* 测量 */
    float      vmeas;            /* 滤波后电压 V              */
    float      imeas;            /* 滤波后电流 mA             */
    float      vraw;             /* 未滤波电压 V              */
    float      iraw;             /* 未滤波电流 mA             */
    int16_t    adc[SMU_ADC_CH_NUM];
    /* 状态 */
    uint16_t   stat;
    int16_t    err;
    uint8_t    j18;
    /* 控制 */
    float      duty;             /* 当前占空比 0..1           */
    float      integ;            /* PI 积分项                 */
    uint32_t   uptime_s;
} smu_state_t;

extern smu_state_t g_smu;

#ifdef __cplusplus
}
#endif

#endif /* SMU_TYPES_H */
