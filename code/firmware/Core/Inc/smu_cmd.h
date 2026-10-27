/**
 * @file   smu_cmd.h
 * @brief  命令层：SCPI 风格命令集，串口与 Web 共用（PRD §6.2/§6.3）
 */
#ifndef SMU_CMD_H
#define SMU_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

void SMU_CmdInit(void);
/* 执行一行命令（不含换行），响应（含结尾 '\n'）写入 resp。
 * 返回响应长度；0 表示无响应（如空行）。 */
int  SMU_CmdExecute(const char *line, char *resp, size_t resp_len);

#ifdef __cplusplus
}
#endif

#endif /* SMU_CMD_H */
