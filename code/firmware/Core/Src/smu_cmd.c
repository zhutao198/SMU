/**
 * @file   smu_cmd.c
 * @brief  命令层：SCPI 风格命令集，串口与 Web 共用（PRD §6.2 / §6.3）
 * @note   所有设置最终都经过本层 → 单一入口，保证两通道行为一致（FR-WEB-6/7）。
 */
#include "smu_cmd.h"
#include "smu_board.h"
#include "smu_cal.h"
#include "smu_mode.h"
#include "smu_ctrl.h"
#include "smu_prot.h"
#include "smu_meas.h"
#include "smu_diag.h"
#include "smu_net.h"
#include "smu_types.h"
#include "smu_uart.h"
#include "smu_rtos.h"
#include "smu_ver.h"
#include "smu_fmt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

extern smu_state_t g_smu;

static smu_mode_t s_pref_out_mode = SMU_MODE_CV;   /* OUTP ON 时回到的输出模式 */

/* ---------------- 工具 ---------------- */
static const char *mode_str(smu_mode_t m)
{
    switch (m) {
    case SMU_MODE_CV:  return "CV";
    case SMU_MODE_CC:  return "CC";
    case SMU_MODE_VM:  return "VM";
    case SMU_MODE_IM:  return "IM";
    default:           return "HIZ";
    }
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int parse_mode(const char *s, smu_mode_t *m)
{
    if (str_eq(s, "CV"))  { *m = SMU_MODE_CV;  return 0; }
    if (str_eq(s, "CC"))  { *m = SMU_MODE_CC;  return 0; }
    if (str_eq(s, "VM"))  { *m = SMU_MODE_VM;  return 0; }
    if (str_eq(s, "IM"))  { *m = SMU_MODE_IM;  return 0; }
    if (str_eq(s, "HIZ")) { *m = SMU_MODE_HIZ; return 0; }
    return -1;
}

/* 模式切换请求 + 等待序列完成（命令任务内阻塞 ≤200ms） */
static int mode_apply(smu_mode_t m)
{
    if (SMU_ModeBusy()) return SMU_ERR_MODE_BUSY;
    int rc = SMU_ModeRequest(m);
    if (rc != 0) return rc;
    for (uint32_t i = 0; i < 40U && SMU_ModeBusy(); i++) vTaskDelay(pdMS_TO_TICKS(5));
    if (SMU_ModeBusy()) return SMU_ERR_MODE_BUSY;
    return 0;
}

/* 响应收尾：追加换行并返回长度 */
static int fin(char *resp, char *p)
{
    p = smu_cat(p, "\n");
    *p = '\0';
    return (int)(p - resp);
}

static int err_resp(char *resp, size_t len, int code)
{
    static const struct { int c; const char *t; } tbl[] = {
        { SMU_ERR_CMD,   "Command error" },
        { SMU_ERR_RANGE, "Parameter out of range" },
        { SMU_ERR_MODE_BUSY, "Mode transition busy" },
        { SMU_ERR_OVP,   "Over-voltage protection latched" },
        { SMU_ERR_OCP,   "Over-current protection latched" },
        { SMU_ERR_ADC,   "ADC communication error" },
        { SMU_ERR_SELFTEST, "Self-test failed" },
    };
    const char *txt = "Unknown error";
    for (unsigned i = 0; i < sizeof(tbl)/sizeof(tbl[0]); i++) {
        if (tbl[i].c == code) { txt = tbl[i].t; break; }
    }
    char *p = resp;
    p = smu_cat_char(p, '-');
    p = smu_cat_int(p, (long)code);
    p = smu_cat(p, ",\"");
    p = smu_cat(p, txt);
    p = smu_cat(p, "\"\n");
    *p = '\0';
    return (int)(p - resp);
}

void SMU_CmdInit(void)
{
    s_pref_out_mode = SMU_MODE_CV;
}

/* ---------------- 命令执行 ---------------- */
int SMU_CmdExecute(const char *line, char *resp, size_t resp_len)
{
    char buf[SMU_LINE_MAX];
    char *argv[4];
    int argc = 0;
    size_t i = 0, j = 0;

    while (line[i] && j + 1U < sizeof(buf)) {
        char c = line[i++];
        if (c == ' ' || c == ',' || c == '\t') {
            if (j > 0 && buf[j-1] != '\0') buf[j++] = '\0';
            continue;
        }
        buf[j++] = c;
    }
    buf[j] = '\0';
    /* 重新切词 */
    for (char *p = buf; *p && argc < 4; ) {
        argv[argc++] = p;
        while (*p) p++;
        p++;
        if (*p == '\0') break;
    }
    if (argc == 0) return 0;
    for (int k = 0; k < argc; k++) {
        if (argv[k][0] == '\0') { argc = k; break; }
    }
    if (argc == 0) return 0;

    /* ---- *IDN? ---- */
    if (str_eq(argv[0], "*IDN?")) {
        char *p = resp;
        p = smu_cat(p, SMU_FW_NAME);
        p = smu_cat(p, ", FW ");
        p = smu_cat(p, SMU_FW_VERSION);
        p = smu_cat(p, ", SN:0001");
        return fin(resp, p);
    }
    /* ---- *RST ---- */
    if (str_eq(argv[0], "*RST")) {
        g_smu.vset = 0.0f; g_smu.iset = 0.0f;
        g_smu.vprot = SMU_V_PROT_DEF; g_smu.iprot = SMU_I_PROT_DEF;
        s_pref_out_mode = SMU_MODE_CV;
        SMU_CtrlSetDuty(0.0f);
        int rc = mode_apply(SMU_MODE_HIZ);
        SMU_ProtClear();
        if (rc != 0) return err_resp(resp, resp_len, rc);
        return fin(resp, smu_cat(resp, "OK"));
    }
    /* ---- *CLS ---- */
    if (str_eq(argv[0], "*CLS")) {
        if (SMU_ProtClear() != 0) return fin(resp, smu_cat(resp, "OK"));
        return fin(resp, smu_cat(resp, "OK"));
    }
    /* ---- SYST:VERS? ---- */
    if (str_eq(argv[0], "SYST:VERS?")) {
        return fin(resp, smu_cat(resp, SMU_FW_VERSION));
    }
    /* ---- SYST:ERR? ---- */
    if (str_eq(argv[0], "SYST:ERR?")) {
        if (g_smu.err == SMU_ERR_NONE) return fin(resp, smu_cat(resp, "0,""No error"""));
        int e = g_smu.err;
        g_smu.err = SMU_ERR_NONE;                    /* 读后清（队列深度 1） */
        return err_resp(resp, resp_len, e);
    }
    /* ---- MODE ---- */
    if (str_eq(argv[0], "MODE")) {
        if (argc < 2) return err_resp(resp, resp_len, SMU_ERR_CMD);
        smu_mode_t m;
        if (parse_mode(argv[1], &m) != 0) return err_resp(resp, resp_len, SMU_ERR_CMD);
        if (m == SMU_MODE_CV || m == SMU_MODE_CC) s_pref_out_mode = m;
        int rc = mode_apply(m);
        if (rc != 0) return err_resp(resp, resp_len, rc);
        return fin(resp, smu_cat(resp, "OK"));
    }
    if (str_eq(argv[0], "MODE?")) {
        return fin(resp, smu_cat(resp, mode_str(g_smu.mode)));
    }
    /* ---- VOLT ---- */
    if (str_eq(argv[0], "VOLT")) {
        if (argc < 2) return err_resp(resp, resp_len, SMU_ERR_CMD);
        float v = smu_atof(argv[1]);
        if (v < 0.0f || v > SMU_V_MAX) return err_resp(resp, resp_len, SMU_ERR_RANGE);
        g_smu.vset = v;
        return fin(resp, smu_cat(resp, "OK"));
    }
    if (str_eq(argv[0], "VOLT?")) {
        return fin(resp, smu_cat_fixed(resp, g_smu.vset, 4));
    }
    /* ---- CURR ---- */
    if (str_eq(argv[0], "CURR")) {
        if (argc < 2) return err_resp(resp, resp_len, SMU_ERR_CMD);
        float i_ma = smu_atof(argv[1]);
        if (i_ma < 0.0f || i_ma > SMU_I_MAX_MA) return err_resp(resp, resp_len, SMU_ERR_RANGE);
        g_smu.iset = i_ma;
        return fin(resp, smu_cat(resp, "OK"));
    }
    if (str_eq(argv[0], "CURR?")) {
        return fin(resp, smu_cat_fixed(resp, g_smu.iset, 4));
    }
    /* ---- VOLT:PROT ---- */
    if (str_eq(argv[0], "VOLT:PROT")) {
        if (argc < 2) return err_resp(resp, resp_len, SMU_ERR_CMD);
        float v = smu_atof(argv[1]);
        if (v < SMU_V_PROT_MIN || v > SMU_V_MAX) return err_resp(resp, resp_len, SMU_ERR_RANGE);
        g_smu.vprot = v;
        return fin(resp, smu_cat(resp, "OK"));
    }
    if (str_eq(argv[0], "VOLT:PROT?")) {
        return fin(resp, smu_cat_fixed(resp, g_smu.vprot, 2));
    }
    /* ---- CURR:PROT ---- */
    if (str_eq(argv[0], "CURR:PROT")) {
        if (argc < 2) return err_resp(resp, resp_len, SMU_ERR_CMD);
        float i_ma = smu_atof(argv[1]);
        if (i_ma < SMU_I_PROT_MIN || i_ma > SMU_I_MAX_MA) return err_resp(resp, resp_len, SMU_ERR_RANGE);
        g_smu.iprot = i_ma;
        return fin(resp, smu_cat(resp, "OK"));
    }
    if (str_eq(argv[0], "CURR:PROT?")) {
        return fin(resp, smu_cat_fixed(resp, g_smu.iprot, 2));
    }
    /* ---- OUTP ---- */
    if (str_eq(argv[0], "OUTP")) {
        if (argc < 2) return err_resp(resp, resp_len, SMU_ERR_CMD);
        int on;
        if (str_eq(argv[1], "ON") || str_eq(argv[1], "1")) on = 1;
        else if (str_eq(argv[1], "OFF") || str_eq(argv[1], "0")) on = 0;
        else return err_resp(resp, resp_len, SMU_ERR_CMD);
        int rc = mode_apply(on ? s_pref_out_mode : SMU_MODE_HIZ);
        if (rc != 0) return err_resp(resp, resp_len, rc);
        return fin(resp, smu_cat(resp, "OK"));
    }
    if (str_eq(argv[0], "OUTP?")) {
        return fin(resp, smu_cat_int(resp, (long)g_smu.outp));
    }
    /* ---- MEAS ---- */
    if (str_eq(argv[0], "MEAS:VOLT?")) {
        return fin(resp, smu_cat_fixed(resp, g_smu.vmeas, 4));
    }
    if (str_eq(argv[0], "MEAS:CURR?")) {
        return fin(resp, smu_cat_fixed(resp, g_smu.imeas, 4));
    }
    if (str_eq(argv[0], "MEAS:ALL?")) {
        char *p = resp;
        p = smu_cat(p, "V=");
        p = smu_cat_fixed(p, g_smu.vmeas, 4);
        p = smu_cat(p, ",I=");
        p = smu_cat_fixed(p, g_smu.imeas, 4);
        return fin(resp, p);
    }
    /* ---- STAT? ---- */
    if (str_eq(argv[0], "STAT?")) {
        return fin(resp, smu_cat_uint(resp, (unsigned long)g_smu.stat));
    }
    /* ---- HW? ----（诊断：回显 PWM/GPIO 硬件实际状态） */
    if (str_eq(argv[0], "HW?")) {
        SMU_CtrlDbgInfo(resp, 128);
        char *p = resp;
        while (*p) p++;
        p = smu_cat(p, ",duty=");
        p = smu_cat_fixed(p, g_smu.duty, 3);
        return fin(resp, p);
    }
    /* ---- J18? ---- */
    if (str_eq(argv[0], "J18?")) {
        uint8_t v = 0;
        if (HAL_GPIO_ReadPin(SMU_J18_1_PORT, SMU_J18_1_PIN) == GPIO_PIN_RESET) v |= 1U;
        if (HAL_GPIO_ReadPin(SMU_J18_2_PORT, SMU_J18_2_PIN) == GPIO_PIN_RESET) v |= 2U;
        g_smu.j18 = v;
        return fin(resp, smu_cat_uint(resp, (unsigned long)v));
    }
    /* ---- RAW? ----（差分原始码值：V=AIN0-AIN1, I=AIN2-AIN3） */
    if (str_eq(argv[0], "RAW?")) {
        char *p = resp;
        p = smu_cat(p, "Vcode=");
        p = smu_cat_int(p, (long)g_smu.adc[SMU_CH_VOLT]);
        p = smu_cat(p, ",Icode=");
        p = smu_cat_int(p, (long)g_smu.adc[SMU_CH_CURR]);
        return fin(resp, p);
    }
    /* ---- I2C? ----（诊断：扫描 I2C 总线，列出应答的 7 位地址） */
    if (str_eq(argv[0], "I2C?")) {
        static const char hex[] = "0123456789ABCDEF";
        char *p = resp;
        uint8_t found = 0;
        p = smu_cat(p, "OK");
        for (uint8_t a = 0x08; a < 0x78; a++) {
            if (ADS1115_Probe(a) == 0) {
                p = smu_cat(p, ",0x");
                *p++ = hex[(a >> 4) & 0x0F];
                *p++ = hex[a & 0x0F];
                found++;
            }
        }
        if (found == 0U) p = smu_cat(p, ",none");
        return fin(resp, p);
    }
    /* ---- ADC? ----（诊断：读取 ctrl 任务已采到的两路原始码值）
     * 注意：cmd 任务**不直接访问 ADS1115**，避免与 ctrl 任务抢总线
     * （曾因此出现通道串味与 I2C 50ms 超时）。 */
    if (str_eq(argv[0], "ADC?")) {
        char *p = resp;
        p = smu_cat(p, "OK");
        p = smu_cat(p, ",V=");
        p = smu_cat_int(p, (long)g_smu.adc[SMU_CH_VOLT]);
        p = smu_cat(p, ",I=");
        p = smu_cat_int(p, (long)g_smu.adc[SMU_CH_CURR]);
        return fin(resp, p);
    }
    /* ---- ADCT? ----（诊断：转换链状态，只读缓存，不碰 I2C） */
    if (str_eq(argv[0], "ADCT?")) {
        char *p = resp;
        p = smu_cat(p, "OK,V=");
        p = smu_cat_int(p, (long)g_smu.adc[SMU_CH_VOLT]);
        p = smu_cat(p, ",I=");
        p = smu_cat_int(p, (long)g_smu.adc[SMU_CH_CURR]);
        p = smu_cat(p, ",cfg=");
        p = smu_cat_uint(p, (unsigned long)SMU_MeasGetCfg());
        p = smu_cat(p, ",stat=");
        p = smu_cat_uint(p, (unsigned long)g_smu.stat);
        p = smu_cat(p, ",err=");
        p = smu_cat_int(p, (long)g_smu.err);
        return fin(resp, p);
    }
    /* ---- SYST:SELF ----（诊断：重新运行上电自检，返回 SELFTEST: OK/FAIL,code） */
    if (str_eq(argv[0], "SYST:SELF")) {
        uint8_t f = SMU_DiagSelfTest();
        char *p = resp;
        p = smu_cat(p, (f == 0U) ? "SELFTEST: OK" : "SELFTEST: FAIL,0x");
        if (f != 0U) {
            static const char hex[] = "0123456789ABCDEF";
            *p++ = hex[(f >> 4) & 0x0F];
            *p++ = hex[f & 0x0F];
        }
        return fin(resp, p);
    }
    /* ---- PHY? ----（诊断：扫描 PHY 地址 0~31，读 PHY ID 寄存器） */
    if (str_eq(argv[0], "PHY?")) {
        static const char hex[] = "0123456789ABCDEF";
        char *p = resp;
        uint8_t found = 0;
        p = smu_cat(p, "OK");
        for (uint8_t a = 0; a < 32U; a++) {
            uint32_t id1 = 0, id2 = 0;
            if (SMU_NetPhyProbe(a, &id1, &id2) == 0) {
                uint32_t oui = ((id1 & 0xFFFFU) << 6) | ((id2 >> 10) & 0x3FU);
                p = smu_cat(p, ",addr=");
                p = smu_cat_uint(p, (unsigned long)a);
                p = smu_cat(p, ",id=0x");
                *p++ = hex[(id1 >> 12) & 0x0F]; *p++ = hex[(id1 >> 8) & 0x0F];
                *p++ = hex[(id1 >> 4) & 0x0F];  *p++ = hex[id1 & 0x0F];
                *p++ = hex[(id2 >> 12) & 0x0F]; *p++ = hex[(id2 >> 8) & 0x0F];
                *p++ = hex[(id2 >> 4) & 0x0F];  *p++ = hex[id2 & 0x0F];
                p = smu_cat(p, ",oui=0x");
                *p++ = hex[(oui >> 16) & 0x0F]; *p++ = hex[(oui >> 12) & 0x0F];
                *p++ = hex[(oui >> 8) & 0x0F];  *p++ = hex[(oui >> 4) & 0x0F];
                *p++ = hex[oui & 0x0F];
                found++;
            }
        }
        if (found == 0U) p = smu_cat(p, ",none");
        return fin(resp, p);
    }
    /* ---- CAL ----（只读 ctrl 任务的滤波缓存，不触发新转换，避免与 ctrl 抢 I2C） */
    if (str_eq(argv[0], "CAL:VOLT:ZERO")) {
        float code = (float)SMU_MeasGetAvgCode(SMU_CH_VOLT);
        smu_cal_t c = SMU_CalGetV();
        SMU_CalSetV(c.k, -c.k * code);
        return fin(resp, smu_cat(smu_cat_fixed(smu_cat(resp, "OK,code="), code, 1), ""));
    }
    if (str_eq(argv[0], "CAL:VOLT:GAIN")) {
        if (argc < 2) return err_resp(resp, resp_len, SMU_ERR_CMD);
        float v = smu_atof(argv[1]);
        if (v <= 0.0f || v > SMU_V_MAX) return err_resp(resp, resp_len, SMU_ERR_RANGE);
        float code = (float)SMU_MeasGetAvgCode(SMU_CH_VOLT);
        smu_cal_t c = SMU_CalGetV();
        if (code == 0.0f) return err_resp(resp, resp_len, SMU_ERR_ADC);
        SMU_CalSetV((v - c.b) / code, c.b);
        return fin(resp, smu_cat(smu_cat_fixed(smu_cat(resp, "OK,code="), code, 1), ""));
    }
    if (str_eq(argv[0], "CAL:CURR:ZERO")) {
        float code = (float)SMU_MeasGetAvgCode(SMU_CH_CURR);
        smu_cal_t c = SMU_CalGetI();
        SMU_CalSetI(c.k, -c.k * code);
        return fin(resp, smu_cat(smu_cat_fixed(smu_cat(resp, "OK,code="), code, 1), ""));
    }
    if (str_eq(argv[0], "CAL:CURR:GAIN")) {
        if (argc < 2) return err_resp(resp, resp_len, SMU_ERR_CMD);
        float i_ma = smu_atof(argv[1]);
        if (i_ma <= 0.0f || i_ma > SMU_I_MAX_MA) return err_resp(resp, resp_len, SMU_ERR_RANGE);
        float code = (float)SMU_MeasGetAvgCode(SMU_CH_CURR);
        smu_cal_t c = SMU_CalGetI();
        if (code == 0.0f) return err_resp(resp, resp_len, SMU_ERR_ADC);
        SMU_CalSetI((i_ma - c.b) / code, c.b);
        return fin(resp, smu_cat(smu_cat_fixed(smu_cat(resp, "OK,code="), code, 1), ""));
    }
    if (str_eq(argv[0], "CAL:SAVE")) {
        return (SMU_CalSave() == 0) ? fin(resp, smu_cat(resp, "OK")) : err_resp(resp, resp_len, SMU_ERR_SELFTEST);
    }
    if (str_eq(argv[0], "CAL:DEF")) {
        SMU_CalDef();
        return fin(resp, smu_cat(resp, "OK"));
    }

    /* 未知命令：回显收到的 token，便于定位串口丢字/串行化等问题 */
    {
        char *p = resp;
        p = smu_cat(p, "-100,\"Command error\",\"");
        for (int i = 0; i < 24 && argv[0][i]; i++) p = smu_cat_char(p, argv[0][i]);
        p = smu_cat(p, "\"\n");
        *p = '\0';
        return (int)(p - resp);
    }
}
