/**
 * @file   smu_cal.c
 * @brief  标定系数 / 网络配置的 Flash 存取（NFR-2：双备份 + CRC + 写入计数）
 * @note   STM32F407VE = **512KB** 版本，Flash 扇区布局：
 *           sector 0-3: 16KB×4 (0x08000000-0x0800FFFF)
 *           sector 4  : 64KB     (0x08010000-0x0801FFFF)
 *           sector 5-7: 128KB×3  (0x08020000-0x0807FFFF)
 *         **不存在 sector 11**（那是 1MB 版本才有），原用 0x080E0000 越界
 *         导致 CAL:SAVE 擦除/编程失败。现 A/B 备份分处 sector 6 与 7，
 *         擦除其中一个不影响另一个。
 */
#include "smu_cal.h"
#include "smu_board.h"
#include <string.h>

#define SMU_NV_SLOT_A_ADDR  0x08040000UL   /* sector 6 */
#define SMU_NV_SLOT_B_ADDR  0x08060000UL   /* sector 7 */

static smu_nv_t s_nv;

/* ---------------- CRC32（软件实现，无外设依赖） ---------------- */
static uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static void nv_defaults(smu_nv_t *nv)
{
    memset(nv, 0, sizeof(*nv));
    nv->magic = SMU_NV_MAGIC;
    nv->write_cnt = 0;
    nv->use_dhcp = 1;
    nv->ip[0] = 192; nv->ip[1] = 168; nv->ip[2] = 7; nv->ip[3] = 200;
    nv->netmask[0] = 255; nv->netmask[1] = 255; nv->netmask[2] = 255; nv->netmask[3] = 0;
    nv->gw[0] = 192; nv->gw[1] = 168; nv->gw[2] = 7; nv->gw[3] = 1;
    nv->tcp_port = 5025;
    /* 默认标定系数 = 理论值（未标定时约 ±2% 精度，PRD §5 注） */
    /* 差分测量默认系数：
     * V = k*Vcode + b，其中 Vcode=(V_TEST_AD-VREF)/LSB
     *   → k = LSB*9.66（每码代表的 J4 电压），b = VREF*9.66（零输入时的偏移）
     * I = k*Icode + b[mA]，VREF 偏置已被差分抵消 → b = 0 */
    nv->cal_v.k = SMU_CAL_V_K_DEFAULT;
    nv->cal_v.b = SMU_CAL_V_B_DEFAULT;
    nv->cal_i.k = SMU_CAL_I_K_DEFAULT;
    nv->cal_i.b = SMU_CAL_I_B_DEFAULT;
    nv->crc32 = crc32_calc((const uint8_t *)nv, offsetof(smu_nv_t, crc32));
}

static int nv_valid(const smu_nv_t *nv)
{
    if (nv->magic != SMU_NV_MAGIC) return 0;
    return nv->crc32 == crc32_calc((const uint8_t *)nv, offsetof(smu_nv_t, crc32));
}

static void flash_read(uint32_t addr, void *buf, uint32_t len)
{
    memcpy(buf, (const void *)addr, len);
}

void SMU_CalInit(void)
{
    smu_nv_t a, b;
    flash_read(SMU_NV_SLOT_A_ADDR, &a, sizeof(a));
    flash_read(SMU_NV_SLOT_B_ADDR, &b, sizeof(b));
    int va = nv_valid(&a), vb = nv_valid(&b);

    if (!va && !vb) { nv_defaults(&s_nv); return; }
    if (va && vb)   { s_nv = (a.write_cnt >= b.write_cnt) ? a : b; return; }
    s_nv = va ? a : b;
}

int SMU_CalSave(void)
{
    static const uint32_t s_sector[2] = {FLASH_SECTOR_6, FLASH_SECTOR_7};
    static const uint32_t s_addr[2]   = {SMU_NV_SLOT_A_ADDR, SMU_NV_SLOT_B_ADDR};
    smu_nv_t old = s_nv;
    smu_nv_t a, b;
    uint32_t words = (sizeof(s_nv) + 3U) / 4U;

    /* 选写入槽：覆盖 write_cnt 较小的那个，另一槽保留上一份好数据。
     * 只擦一个扇区（128KB 扇区擦除需 ~1s），擦除中断也不会毁掉备份。 */
    flash_read(SMU_NV_SLOT_A_ADDR, &a, sizeof(a));
    flash_read(SMU_NV_SLOT_B_ADDR, &b, sizeof(b));
    int va = nv_valid(&a), vb = nv_valid(&b);
    uint32_t slot;
    if (va && !vb)      slot = 1U;                                  /* B 无效 → 写 B */
    else if (!va && vb) slot = 0U;                                  /* A 无效 → 写 A */
    else if (va && vb)  slot = (a.write_cnt >= b.write_cnt) ? 1U : 0U; /* 覆盖旧的 */
    else                slot = 0U;                                  /* 都无效 → 写 A */

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                          FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    FLASH_EraseInitTypeDef er = {0};
    uint32_t err = 0;
    er.TypeErase    = FLASH_TYPEERASE_SECTORS;
    er.Sector       = s_sector[slot];
    er.NbSectors    = 1;
    er.VoltageRange = FLASH_VOLTAGE_RANGE_3;   /* 128KB 扇区用 range 3 */
    if (HAL_FLASHEx_Erase(&er, &err) != HAL_OK) { HAL_FLASH_Lock(); return -1; }

    s_nv.write_cnt = old.write_cnt + 1U;
    s_nv.crc32 = crc32_calc((const uint8_t *)&s_nv, offsetof(smu_nv_t, crc32));
    const uint32_t *src = (const uint32_t *)&s_nv;

    for (uint32_t i = 0; i < words; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, s_addr[slot] + i * 4U, src[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            s_nv = old;
            return -1;
        }
    }
    HAL_FLASH_Lock();
    return 0;
}

void SMU_CalDef(void)
{
    smu_nv_t def;
    nv_defaults(&def);
    s_nv.cal_v = def.cal_v;
    s_nv.cal_i = def.cal_i;
}

void SMU_CalSetV(float k, float b) { s_nv.cal_v.k = k; s_nv.cal_v.b = b; }
void SMU_CalSetI(float k, float b) { s_nv.cal_i.k = k; s_nv.cal_i.b = b; }
smu_cal_t SMU_CalGetV(void) { return s_nv.cal_v; }
smu_cal_t SMU_CalGetI(void) { return s_nv.cal_i; }
const smu_nv_t *SMU_CalNv(void) { return &s_nv; }
