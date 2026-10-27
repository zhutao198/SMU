/**
 * @file   smu_mode.h
 * @brief  模式状态机（含安全切换时序，PRD §4.1 FR-MODE-6 / §7 SR-4）
 */
#ifndef SMU_MODE_H
#define SMU_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "smu_types.h"

void SMU_ModeInit(void);
/* 请求切换模式：0=成功；SMU_ERR_MODE_BUSY=上一序列未完成 */
int  SMU_ModeRequest(smu_mode_t m);
int  SMU_ModeBusy(void);
void SMU_ModeTick(void);          /* 由控制任务按 SMU_SEQ_TICK_MS 调用 */

#ifdef __cplusplus
}
#endif

#endif /* SMU_MODE_H */
