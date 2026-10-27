/**
 * @file   smu_fmt.c
 * @brief  自建小型格式化库实现（见 smu_fmt.h 说明：不使用 newlib printf）
 */
#include "smu_fmt.h"

static char *put_uint(char *p, unsigned long v)
{
    char buf[12];
    int i = 0;
    do {
        buf[i++] = (char)('0' + (int)(v % 10UL));
        v /= 10UL;
    } while (v != 0);
    while (i > 0) {
        *p++ = buf[--i];
    }
    return p;
}

/* 追加定点数，指定位数（不足补 0），如 pad(7,3) -> "007" */
static char *put_uint_pad(char *p, unsigned long v, int width)
{
    char buf[12];
    int i = 0, j;
    do {
        buf[i++] = (char)('0' + (int)(v % 10UL));
        v /= 10UL;
    } while (v != 0);
    for (j = i; j < width; j++) {
        *p++ = '0';
    }
    while (i > 0) {
        *p++ = buf[--i];
    }
    return p;
}

char *smu_cat(char *p, const char *s)
{
    if (s == NULL) return p;
    while (*s) {
        *p++ = *s++;
    }
    return p;
}

char *smu_cat_char(char *p, char c)
{
    *p++ = c;
    return p;
}

char *smu_cat_int(char *p, long v)
{
    unsigned long u;
    if (v < 0) {
        *p++ = '-';
        u = (unsigned long)(-(v + 1)) + 1UL;
    } else {
        u = (unsigned long)v;
    }
    return put_uint(p, u);
}

char *smu_cat_uint(char *p, unsigned long v)
{
    return put_uint(p, v);
}

char *smu_cat_fixed(char *p, float v, int dec)
{
    long scale = 1, scaled, ip, fp;
    int i;

    if (dec < 0) dec = 0;
    if (dec > 6) dec = 6;
    if (v < 0.0f) {
        *p++ = '-';
        v = -v;
    }
    for (i = 0; i < dec; i++) scale *= 10L;
    scaled = (long)(v * (float)scale + 0.5f);
    if (scaled < 0) scaled = 0;
    ip = scaled / scale;
    fp = scaled % scale;
    p = put_uint(p, (unsigned long)ip);
    if (dec > 0) {
        *p++ = '.';
        p = put_uint_pad(p, (unsigned long)fp, dec);
    }
    return p;
}

char *smu_cat_ip(char *p, const unsigned char *ip4)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (i > 0) *p++ = '.';
        p = put_uint(p, (unsigned long)ip4[i]);
    }
    return p;
}

char *smu_cat_mac(char *p, const unsigned char *m)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < 6; i++) {
        if (i > 0) *p++ = ':';
        *p++ = hex[(m[i] >> 4) & 0x0F];
        *p++ = hex[m[i] & 0x0F];
    }
    return p;
}

int smu_fmt_f(char *buf, float v, int dec)
{
    char *p = buf;
    p = smu_cat_fixed(p, v, dec);
    *p = '\0';
    return (int)(p - buf);
}

float smu_atof(const char *s)
{
    float v = 0.0f;
    int neg = 0, frac = 0, div = 1;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    while (*s >= '0' && *s <= '9') {
        v = v * 10.0f + (float)(*s - '0');
        if (frac) div *= 10;
        s++;
    }
    if (*s == '.') {
        frac = 1;
        s++;
        while (*s >= '0' && *s <= '9') {
            v = v * 10.0f + (float)(*s - '0');
            div *= 10;
            s++;
        }
    }
    v = v / (float)div;
    return neg ? -v : v;
}
