/**
 * @file   syscalls.c
 * @brief  newlib 堆支持（_sbrk），独立构建用
 * @note   libnosys 的 _sbrk stub 不允许分配内存，导致 snprintf("%f") 走
 *         _dtoa -> malloc 失败链，最终 BusFault（BFAR 为垃圾地址）。
 *         这里用链接脚本的 end ~ 栈顶之间空间实现真正的堆。
 */
#include <stddef.h>
#include <sys/types.h>

extern char end;      /* bss 结束（链接脚本 _end 附近） */
extern char _estack;  /* 栈顶 */

static char *s_heap_end = NULL;

char *_sbrk(int incr)
{
    char *prev;

    if (s_heap_end == NULL) {
        s_heap_end = &end;
    }
    prev = s_heap_end;

    /* 不允许超过栈顶（留 2KB 余量防溢出） */
    if ((s_heap_end + incr) > (&_estack - 2048)) {
        return (char *)-1;
    }
    s_heap_end += incr;
    return prev;
}
