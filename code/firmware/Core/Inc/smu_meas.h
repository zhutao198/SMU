/**
 * @file   smu_meas.h
 * @brief  测量链：ADS1115 驱动 + 工程量换算 + 数字滤波（PRD §4.3）
 */
#ifndef SMU_MEAS_H
#define SMU_MEAS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ADS1115 读写（返回 0 成功） */
int  ADS1115_WriteReg(uint8_t reg, uint16_t val);
int  ADS1115_ReadReg(uint8_t reg, uint16_t *val);
int  ADS1115_ConvertOnce(uint8_t ch, int16_t *out);
float SMU_MeasAvgCode(uint8_t ch);    /* 16 次转换平均，标定用 */
int  ADS1115_Probe(uint8_t addr7);    /* I2C 探测：返回 0 表示有器件应答（诊断用） */
uint16_t SMU_MeasGetCfg(void);       /* ctrl 任务最后读回的 config（诊断用，不碰 I2C） */
int16_t  SMU_MeasGetAvgCode(uint8_t ch); /* 8 点滤波平均码值（标定用，只读缓存） */

/* 模块接口 */
void SMU_MeasInit(void);
void SMU_MeasTick(void);          /* 控制环节拍内调用，轮转一个通道 */
float SMU_GetVolt(void);
float SMU_GetCurr(void);
void SMU_MeasGetRaw(int16_t *buf, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* SMU_MEAS_H */
