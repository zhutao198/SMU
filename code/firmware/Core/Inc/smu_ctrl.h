/**
 * @file   smu_ctrl.h
 * @brief  输出执行：GPIO 通断 + PWM 占空比 + 开环/闭环调节（PRD §4.2）
 */
#ifndef SMU_CTRL_H
#define SMU_CTRL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void SMU_CtrlInit(void);
void SMU_CtrlTick(void);          /* 控制环，250Hz（PR-8） */
void SMU_CtrlSetDuty(float d);    /* 0..1 */
float SMU_CtrlGetDuty(void);
/* 立即安全关断（保护/急停路径直接调用，不走状态机） */
void SMU_CtrlShutdown(void);
/* 复位控制状态（模式提交/切换时调用，清积分/抖动/软启动） */
void SMU_CtrlReset(void);
/* 硬件状态回显（诊断用）：写入 buf，格式 "ccr2=..,arr=..,en=..,itest=..,ivtest=.." */
void SMU_CtrlDbgInfo(char *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* SMU_CTRL_H */
