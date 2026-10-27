/**
 * @file   smu_app.h
 * @brief  应用初始化入口（由 main.c 的 USER CODE BEGIN 2 调用）
 */
#ifndef SMU_APP_H
#define SMU_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* 在 osKernelInitialize() 之后、osKernelStart() 之前调用 */
void SMU_AppInit(void);

#ifdef __cplusplus
}
#endif

#endif /* SMU_APP_H */
