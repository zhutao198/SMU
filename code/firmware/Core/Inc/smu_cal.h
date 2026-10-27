/**
 * @file   smu_cal.h
 * @brief  标定系数与网络配置的 Flash 存取（PRD §4.3 FR-MEAS-6 / NFR-2）
 */
#ifndef SMU_CAL_H
#define SMU_CAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "smu_types.h"

void      SMU_CalInit(void);                 /* 上电加载（失败用默认值） */
int       SMU_CalSave(void);                 /* 写入 Flash，0=成功        */
void      SMU_CalDef(void);                  /* 恢复默认系数              */
void      SMU_CalSetV(float k, float b);
void      SMU_CalSetI(float k, float b);
smu_cal_t SMU_CalGetV(void);
smu_cal_t SMU_CalGetI(void);
const smu_nv_t *SMU_CalNv(void);

#ifdef __cplusplus
}
#endif

#endif /* SMU_CAL_H */
