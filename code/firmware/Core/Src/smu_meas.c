/**
 * @file   smu_meas.c
 * @brief  ADS1115 驱动 + 工程量换算 + 数字滤波（PRD §4.3）
 * @note   采样策略：控制环 250Hz，轮转 2 个差分通道 → 电压/电流各 125Hz（PR-8）。
 *         转换完成用固定延时 2ms 判断（单次模式 860SPS 转换仅 1.2ms）。
 *         **只有 ctrl 任务访问 ADS1115**；cmd 任务只读 g_smu.adc[] 缓存。
 */
#include "smu_meas.h"
#include "smu_board.h"
#include "smu_cal.h"
#include "smu_types.h"
#include "smu_rtos.h"
#include <string.h>

extern smu_state_t g_smu;

/* I2C 互斥锁：ctrl 任务（250Hz）与 cmd 任务（I2C?/ADC?/ADCT?）都会访问
 * ADS1115。HAL I2C 不是线程安全的，并发访问会导致寄存器写入静默失败
 * （曾因此电压通道读到电流通道的值、读数虚高到 16V）。 */
static SemaphoreHandle_t s_i2c_mux;
/* ctrl 任务最后一次读回的 ADS1115 config（诊断用，cmd 任务不直接访问 I2C） */
static uint16_t s_cfg_last;

static int i2c_lock(void)
{
    return (xSemaphoreTake(s_i2c_mux, pdMS_TO_TICKS(50)) == pdTRUE) ? 0 : -1;
}

static void i2c_unlock(void)
{
    xSemaphoreGive(s_i2c_mux);
}

/* ---------------- ADS1115 寄存器读写（内部无锁版本） ---------------- */
static int i2c_write_raw(uint8_t reg, uint16_t val)
{
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (uint8_t)(val >> 8);
    buf[2] = (uint8_t)(val & 0xFF);
    for (int i = 0; i < 3; i++) {
        if (HAL_I2C_Master_Transmit(&hi2c1, SMU_ADS1115_ADDR, buf, 3, 20) == HAL_OK) return 0;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return -1;
}

static int i2c_read_raw(uint8_t reg, uint16_t *val)
{
    uint8_t buf[2];
    for (int i = 0; i < 3; i++) {
        if (HAL_I2C_Master_Transmit(&hi2c1, SMU_ADS1115_ADDR, &reg, 1, 20) != HAL_OK) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        if (HAL_I2C_Master_Receive(&hi2c1, SMU_ADS1115_ADDR, buf, 2, 20) != HAL_OK) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        *val = (uint16_t)((buf[0] << 8) | buf[1]);
        return 0;
    }
    return -1;
}

/* ---------------- ADS1115 寄存器读写（公开接口，自带锁） ---------------- */
int ADS1115_WriteReg(uint8_t reg, uint16_t val)
{
    int rc;
    if (i2c_lock() != 0) return -1;
    rc = i2c_write_raw(reg, val);
    i2c_unlock();
    return rc;
}

int ADS1115_ReadReg(uint8_t reg, uint16_t *val)
{
    int rc;
    if (i2c_lock() != 0) return -1;
    rc = i2c_read_raw(reg, val);
    i2c_unlock();
    return rc;
}

/* 启动单次转换并等待完成。
 * 用 OS 位轮询 + **config 读回校验**：
 * - 写 config 后 OS=1 启动转换，完成后硬件自动清零
 * - 860SPS 约 1.2ms；若写入未生效（保持上电默认 128SPS）约 7.8ms，轮询最多 12ms
 * - 读回校验 MUX/PGA：曾因写入静默失败导致器件保持旧 MUX，
 *   结果电压通道读到电流通道的值，8 点滤波缓冲被污染（V 读数虚高到 16V）
 * ch: 0=电压(AIN0-AIN1) 1=电流(AIN2-AIN3) */
#define ADS_CFG_KEYBITS         0x7FE3U   /* MUX+PGA+MODE+DR+COMP_QUE（不含 OS） */

int ADS1115_ConvertOnce(uint8_t ch, int16_t *out)
{
    uint16_t cfg = 0, dummy = 0;
    uint16_t cfgw = (ch == SMU_CH_CURR) ? ADS1115_CFG_CURR : ADS1115_CFG_VOLT;

    /* 整个过程持锁：写 config→校验→等转换→读 conv 必须原子。
     * 曾只在单次 I2C 传输加锁，ctrl 任务（P4）能在多次传输之间抢占
     * cmd 任务（P2）改写 config，导致约 30% 的读数串味。 */
    if (i2c_lock() != 0) return -1;

    for (int retry = 0; retry < 3; retry++) {
        if (i2c_write_raw(ADS1115_REG_CONFIG, cfgw) != 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        /* 读回 config 记录（诊断用），并确认 MUX/PGA 已切换 */
        if (i2c_read_raw(ADS1115_REG_CONFIG, &cfg) == 0) {
            s_cfg_last = cfg;
            if ((cfg & ADS_CFG_KEYBITS) != (cfgw & ADS_CFG_KEYBITS)) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }
        }

        /* 等转换完成：单次模式 860SPS 仅需 1.2ms，等 2ms 足够。
         * 再多读一次 conv 丢弃，保证取到的是**本次**转换的结果
         * （器件若推迟接受 config，第一次读到的仍是上一次的旧值）。 */
        vTaskDelay(pdMS_TO_TICKS(2));
        if (i2c_read_raw(ADS1115_REG_CONV, &dummy) != 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
        if (i2c_read_raw(ADS1115_REG_CONV, &dummy) == 0) {
            *out = (int16_t)dummy;
            i2c_unlock();
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    i2c_unlock();
    return -1;
}

/* I2C 探测（诊断）：addr7 为 7 位地址，返回 0 表示有器件应答 */
int ADS1115_Probe(uint8_t addr7)
{
    int rc;
    if (i2c_lock() != 0) return -1;
    rc = (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr7 << 1), 2, 5) == HAL_OK) ? 0 : -1;
    i2c_unlock();
    return rc;
}

/* 连续 N 次转换取平均（标定用） */
float SMU_MeasAvgCode(uint8_t ch)
{
    int32_t sum = 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < 16U; i++) {
        int16_t c;
        if (ADS1115_ConvertOnce(ch, &c) == 0) { sum += c; n++; }
    }
    return (n > 0) ? ((float)sum / (float)n) : 0.0f;
}

/* ---------------- 滤波 ---------------- */
static int16_t s_fbuf[SMU_ADC_CH_NUM][SMU_MEAS_FILTER_LEN];
static uint8_t s_fidx;
static uint8_t s_ch;

void SMU_MeasInit(void)
{
    if (s_i2c_mux == NULL) {
        s_i2c_mux = xSemaphoreCreateMutex();
    }
    memset(s_fbuf, 0, sizeof(s_fbuf));
    s_fidx = 0;
    s_ch = 0;
    g_smu.adc[0] = 0;
    g_smu.adc[1] = 0;
}

void SMU_MeasTick(void)
{
    int16_t code = 0;
    uint8_t ch = s_ch;
    s_ch = (uint8_t)((s_ch + 1U) % SMU_ADC_CH_NUM);

    if (ADS1115_ConvertOnce(ch, &code) != 0) {
        g_smu.stat &= (uint16_t)~SMU_ST_ADC_OK;
        g_smu.err = SMU_ERR_ADC;
        return;
    }
    g_smu.stat |= SMU_ST_ADC_OK;
    if (g_smu.err == SMU_ERR_ADC) g_smu.err = SMU_ERR_NONE;

    s_fbuf[ch][s_fidx] = code;
    if (ch == SMU_CH_CURR) s_fidx = (uint8_t)((s_fidx + 1U) % SMU_MEAS_FILTER_LEN);
    g_smu.adc[ch] = code;

    /* 换算成工程量（标定系数 y = k*x + b，FR-MEAS-6） */
    if (ch == SMU_CH_VOLT || ch == SMU_CH_CURR) {
        int32_t sum = 0;
        for (uint8_t i = 0; i < SMU_MEAS_FILTER_LEN; i++) sum += s_fbuf[ch][i];
        float avg = (float)sum / (float)SMU_MEAS_FILTER_LEN;
        if (ch == SMU_CH_VOLT) {
            smu_cal_t c = SMU_CalGetV();
            g_smu.vraw = c.k * (float)code + c.b;
            g_smu.vmeas = c.k * avg + c.b;
        } else {
            smu_cal_t c = SMU_CalGetI();
            g_smu.iraw = c.k * (float)code + c.b;
            g_smu.imeas = c.k * avg + c.b;
        }
    }
}

float SMU_GetVolt(void) { return g_smu.vmeas; }

/* 供命令层诊断：只读缓存，不碰 I2C（cmd 任务不得与 ctrl 任务抢 ADS1115） */
uint16_t SMU_MeasGetCfg(void) { return s_cfg_last; }

/* 8 点滤波后的平均码值（标定用；cmd 任务只读缓存，不触发新的转换） */
int16_t SMU_MeasGetAvgCode(uint8_t ch)
{
    int32_t sum = 0;
    if (ch >= SMU_ADC_CH_NUM) return 0;
    for (uint8_t i = 0; i < SMU_MEAS_FILTER_LEN; i++) sum += s_fbuf[ch][i];
    return (int16_t)(sum / (int32_t)SMU_MEAS_FILTER_LEN);
}
float SMU_GetCurr(void) { return g_smu.imeas; }

void SMU_MeasGetRaw(int16_t *buf, uint8_t len)
{
    for (uint8_t i = 0; i < len && i < SMU_ADC_CH_NUM; i++) buf[i] = g_smu.adc[i];
}
