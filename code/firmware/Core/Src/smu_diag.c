/**
 * @file   smu_diag.c
 * @brief  上电自检 + 看门狗喂狗（PRD §4.5 FR-DIAG / §7 SR-6）
 */
#include "smu_diag.h"
#include "smu_board.h"
#include "smu_cal.h"
#include "smu_meas.h"
#include "smu_types.h"
#include "smu_rtos.h"
#include <string.h>

extern smu_state_t g_smu;
extern IWDG_HandleTypeDef hiwdg;

uint8_t SMU_DiagSelfTest(void)
{
    uint8_t fail = 0;
    uint16_t cfg = 0;
    int i;

    /* ADS1115：I2C 能读到寄存器即认为在线。
     * 不强制 config == 0x8583：器件未真正断电时会保留上次写入的配置值，
     * 精确比对会误判（PRD FR-DIAG-1 只要求"在线"）。 */
    for (i = 0; i < 3; i++) {
        if (ADS1115_ReadReg(ADS1115_REG_CONFIG, &cfg) == 0) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (i >= 3) {
        fail |= SMU_DT_ADS1115;
    } else {
        /* 主动写入一次配置，确保 ADS1115 处于已知状态 */
        ADS1115_WriteReg(ADS1115_REG_CONFIG, ADS1115_CFG_VOLT);
    }

    /* NV 区：加载后应有效（SMU_CalInit 失败时装入默认值，不视为致命） */
    const smu_nv_t *nv = SMU_CalNv();
    if (nv->magic != SMU_NV_MAGIC) fail |= SMU_DT_NV;

    if (fail == 0U) {
        g_smu.stat |= SMU_ST_READY;
        g_smu.stat |= SMU_ST_ADC_OK;
    } else {
        g_smu.stat &= (uint16_t)~SMU_ST_READY;
        if (fail & SMU_DT_ADS1115) g_smu.err = SMU_ERR_ADC;
        else if (fail & SMU_DT_NV)  g_smu.err = SMU_ERR_SELFTEST;
    }
    return fail;
}

void SMU_DiagFeedWdg(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}
