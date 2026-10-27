/**
 * @file   smu_fmt.h
 * @brief  自建小型格式化库（不依赖 newlib printf/scanf）
 * @note   为什么不用 printf：本工程链接 nosys + 完整 newlib 时，
 *         snprintf 内部走 _REENT/impure 结构链，在裸机环境下会访问
 *         无效地址（BFAR=0x00B8xxxx）导致 BusFault。
 *         嵌入式固件用自建定点格式化更可靠、更小（省 ~35KB Flash）。
 *         所有函数返回追加后的缓冲区末尾指针，支持链式拼接。
 */
#ifndef SMU_FMT_H
#define SMU_FMT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

char *smu_cat(char *p, const char *s);              /* 追加字符串          */
char *smu_cat_int(char *p, long v);                 /* 追加有符号整数      */
char *smu_cat_uint(char *p, unsigned long v);       /* 追加无符号整数      */
char *smu_cat_fixed(char *p, float v, int dec);     /* 追加定点小数        */
char *smu_cat_ip(char *p, const unsigned char *ip4);/* 追加 "a.b.c.d"      */
char *smu_cat_mac(char *p, const unsigned char *m); /* 追加 "AA:BB:.."     */
char *smu_cat_char(char *p, char c);                /* 追加单字符          */

/* 浮点 -> 定点字符串（独立缓冲，兼容旧接口） */
int   smu_fmt_f(char *buf, float v, int dec);

/* 字符串 -> 浮点（替代 atof） */
float smu_atof(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* SMU_FMT_H */
