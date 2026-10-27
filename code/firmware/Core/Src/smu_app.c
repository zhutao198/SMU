/**
 * @file   smu_app.c
 * @brief  应用初始化编排 + FreeRTOS 任务（PRD §8 里程碑 M1）
 * @note   任务优先级设计（FR-WEB-8：网络任务不得影响控制环）：
 *           ctrl(4) > cmd(2) > web(1) > mon(0)
 */
#include "smu_app.h"
#include "smu_board.h"
#include "smu_cal.h"
#include "smu_cmd.h"
#include "smu_ctrl.h"
#include "smu_diag.h"
#include "smu_meas.h"
#include "smu_mode.h"
#include "smu_prot.h"
#include "smu_types.h"
#include "smu_uart.h"
#include "smu_web.h"
#include "smu_net.h"
#include "smu_rtos.h"
#include "smu_ver.h"
#include "smu_fmt.h"
#include <string.h>
#include <stdio.h>

extern smu_state_t g_smu;

/* 全局运行状态（唯一定义处） */
smu_state_t g_smu;

/* 调试阶段标记（与 freertos_hooks.c 联动，J-Link 可读；正式版可删） */
extern volatile uint32_t g_dbg_stage;

/* 任务函数前向声明（task_plat 中引用） */
static void task_ctrl(void *arg);
static void task_cmd(void *arg);
static void task_mon(void *arg);

#if SMU_IWDG_ENABLE
void MX_IWDG_Init(void);
#endif

static char banner[192];       /* 上电 banner 缓冲（任务栈较大，放文件作用域更稳） */

/* 命令链路诊断计数器（J-Link 读内存定位断点） */
volatile uint32_t g_dbg_rx_bytes = 0;    /* RX 中断收到的字节数   */
volatile uint32_t g_dbg_cmd_loops = 0;   /* task_cmd 循环次数     */
volatile uint32_t g_dbg_cmd_lines = 0;   /* task_cmd 读到的行数   */

/* ---------------- 平台/网络任务（最高优先级，先完成初始化） --------------- */
/* 所有带延时的初始化（I2C 读 ADC、以太网）都必须在这里做：
   调度器启动前 SysTick 已由 HAL_Init 启用，FreeRTOS 的 xTaskIncrementTick
   会在链表未初始化时遍历野指针（BusFault）。 */
static void task_plat(void *arg)
{
    uint16_t phy_div = 0;
    TickType_t last;

    (void)arg;
    g_dbg_stage = 1;  SMU_CalInit();
    g_dbg_stage = 2;  SMU_MeasInit();
    g_dbg_stage = 3;  SMU_ModeInit();
    g_dbg_stage = 4;  SMU_CtrlInit();
    g_dbg_stage = 5;  SMU_ProtInit();
    g_dbg_stage = 6;  SMU_CmdInit();
    g_dbg_stage = 7;  SMU_UartInit();
#if SMU_IWDG_ENABLE
    MX_IWDG_Init();                     /* 看门狗（ctrl 任务随后开始喂狗） */
#endif

    /* 等模拟电源与 ADS1115 上电稳定（LM7705 负电源、LMR64010 升压都有启动时间） */
    vTaskDelay(pdMS_TO_TICKS(100));

    {
        uint8_t fail = SMU_DiagSelfTest();      /* FR-DIAG-1 */
        g_dbg_stage = 8;

        /* 上电 banner（首次上电调试用：版本/自检）
         * 全部使用自建格式化（smu_cat_*），不用 printf（见 smu_fmt.h） */
        {
            char *p = banner;
            p = smu_cat(p, "\r\n=== " SMU_FW_NAME " FW " SMU_FW_VERSION " ===\r\n");
            p = smu_cat(p, "SELFTEST: ");
            p = smu_cat(p, (fail == 0U) ? "OK" : "FAIL");
            p = smu_cat(p, " (ADS1115 ");
            p = smu_cat(p, (fail & SMU_DT_ADS1115) ? "FAIL" : "OK");
            p = smu_cat(p, ")\r\n");
            *p = '\0';
            SMU_UartSend(banner, (uint32_t)(p - banner));
        }

        if (fail != 0U) {
            /* 自检失败：保持 HIZ，串口上报（FR-DIAG-2） */
            SMU_UartSend("SMU self-test FAILED, output disabled\r\n", 38);
        } else {
            SMU_UartSend("READY. Send *IDN? to verify.\r\n", 31);
        }
    }

    /* LwIP 初始化（以太网），随后创建其余任务 */
    g_dbg_stage = 20; SMU_NetInit();
    g_dbg_stage = 21;

    g_dbg_stage = 22; xTaskCreate(task_ctrl, "ctrl", 512, NULL, 4, NULL);
    g_dbg_stage = 23; xTaskCreate(task_cmd,  "cmd",  512, NULL, 2, NULL);
    g_dbg_stage = 24; xTaskCreate(task_mon,  "mon",  256, NULL, 0, NULL);
    g_dbg_stage = 25; SMU_WebInit();            /* HTTP 服务（netconn） */
    g_dbg_stage = 99;

    /* 本任务转为以太网收包轮询（AC-13：拔插网线不影响控制环）。
     * PHY 状态轮询在 task_mon（最低优先级）里做，避免 MDIO 超时饿死控制环。 */
    (void)phy_div;
    last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(5));
        SMU_NetPoll();
    }
}

/* ---------------- 控制任务（250Hz） ---------------- */
static void task_ctrl(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000U / SMU_CTRL_HZ);
    uint16_t seq_div = 0, j18_div = 0;
    for (;;) {
        vTaskDelayUntil(&last, period);

        SMU_ModeTick();                       /* 模式切换序列（10ms 节拍） */
        if (++seq_div >= (SMU_CTRL_HZ * SMU_SEQ_TICK_MS / 1000U)) {
            seq_div = 0;
        }
        SMU_MeasTick();                       /* 轮转一个 ADC 通道 */
        SMU_CtrlTick();                       /* 占空比 / 软启动 */
        SMU_ProtCheck();                      /* 保护 */

        if (++j18_div >= SMU_CTRL_HZ) {       /* 1Hz 更新拨码状态 */
            j18_div = 0;
            uint8_t v = 0;
            if (HAL_GPIO_ReadPin(SMU_J18_1_PORT, SMU_J18_1_PIN) == GPIO_PIN_RESET) v |= 1U;
            if (HAL_GPIO_ReadPin(SMU_J18_2_PORT, SMU_J18_2_PIN) == GPIO_PIN_RESET) v |= 2U;
            g_smu.j18 = v;
            g_smu.uptime_s++;
        }
        SMU_DiagFeedWdg();                    /* 控制环活着 = 系统健康 */
    }
}

/* ---------------- 串口命令任务 ---------------- */
extern volatile uint32_t g_dbg_cmd_loops;
extern volatile uint32_t g_dbg_cmd_lines;

static void task_cmd(void *arg)
{
    (void)arg;
    char line[SMU_LINE_MAX], resp[128];
    for (;;) {
        g_dbg_cmd_loops++;
        if (SMU_UartGetLine(line, sizeof(line), 100) == 0) {
            g_dbg_cmd_lines++;
            int n = SMU_CmdExecute(line, resp, sizeof(resp));
            if (n > 0) SMU_UartSend(resp, (uint32_t)n);
        }
    }
}

/* ---------------- 监控任务（1Hz，诊断 + PHY 轮询） ---------------- */
/* PHY 轮询放在最低优先级：即使 PHY 无响应（每次 MDIO 访问超时 100ms），
   也不会影响控制环与命令链路。 */
static void task_mon(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        SMU_NetPhyPoll();               /* PHY 链接状态（100ms） */
        /* M1 预留：片内温度、运行统计等（FR-DIAG-4，M3 实现） */
    }
}

void SMU_AppInit(void)
{
    /* 只创建平台任务（调度器启动前创建任务是安全的：只分配内存并入就绪链表）。
     * 所有带延时的初始化都放在 task_plat 中（调度器启动后）执行。 */
    g_dbg_stage = 10;
    if (xTaskCreate(task_plat, "plat", 512, NULL, 5, NULL) != pdPASS) {
        for (;;) { }                            /* 平台任务创建失败：无法恢复 */
    }
}
