/**
 * @file   smu_prot.h
 * @brief  保护：过压/过流/ADC 饱和，锁存语义（PRD §7 SR-2/3/5）
 */
#ifndef SMU_PROT_H
#define SMU_PROT_H

#ifdef __cplusplus
extern "C" {
#endif

void SMU_ProtInit(void);
void SMU_ProtCheck(void);         /* 控制环节拍内调用 */
int  SMU_ProtClear(void);         /* *CLS：0=成功，<0=无锁存可清 */

#ifdef __cplusplus
}
#endif

#endif /* SMU_PROT_H */
