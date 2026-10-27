/**
 * @file   smu_diag.h
 * @brief  上电自检 + 看门狗喂狗（PRD §4.5 FR-DIAG / §7 SR-6）
 */
#ifndef SMU_DIAG_H
#define SMU_DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 自检项位域 */
#define SMU_DT_ADS1115     0x01U   /* I2C 扫描 + 配置寄存器读回 */
#define SMU_DT_NV          0x02U   /* Flash 标定区 CRC          */

uint8_t SMU_DiagSelfTest(void);   /* 返回失败位域，0=全部通过 */
void   SMU_DiagFeedWdg(void);

#ifdef __cplusplus
}
#endif

#endif /* SMU_DIAG_H */
