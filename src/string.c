#include "kernel.h"

void *memset(void *d, int c, size_t n)
{
    u8 *p = d;
    while (n--)
        *p++ = (u8)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    u8 *dp = d;
    const u8 *sp = s;
    while (n--)
        *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    u8 *dp = d;
    const u8 *sp = s;
    if (dp < sp) {
        while (n--)
            *dp++ = *sp++;
    } else {
        while (n--)
            dp[n] = sp[n];
    }
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (u8)*a - (u8)*b;
}

void strlcpy(char *d, const char *s, size_t n)
{
    if (!n)
        return;
    while (--n && *s)
        *d++ = *s++;
    *d = 0;
}

int atoi(const char *s)
{
    int sign = 1, v = 0;
    if (*s == '-')
        sign = -1, s++;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return sign * v;
}
