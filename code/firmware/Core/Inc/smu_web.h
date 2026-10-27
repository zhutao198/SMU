/**
 * @file   smu_web.h
 * @brief  Web 服务：内置 HTTP（LwIP netconn），路由见 PRD §6.3
 */
#ifndef SMU_WEB_H
#define SMU_WEB_H

#ifdef __cplusplus
extern "C" {
#endif

void SMU_WebInit(void);           /* 在 MX_LWIP_Init() 之后调用 */

#ifdef __cplusplus
}
#endif

#endif /* SMU_WEB_H */
