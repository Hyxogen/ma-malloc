#ifndef MA_LIBC_STRING_H
#define MA_LIBC_STRING_H

#include <ma/libc/internal.h>
#include <stddef.h>

#ifndef MA_USE_LIBFT
#define MA_USE_LIBFT 0
#endif

#if MA_USE_LIBFT
#include <ft/string.h>

/* Libft doesn't have a strerror (yet) */
char *ma_strerror(int errnum);
#elif !MA_PLATFORM_BARE
#include <string.h>

inline char *ma_strerror(int errnum) { return MA_LIBC_PREFIX(strerror)(errnum); }
#endif

#if MA_PLATFORM_BARE
char *ma_strerror(int errnum);
void *ma_memcpy(void *dest, const void *src, size_t n);
void *ma_memset(void *dest, int c, size_t n);
#else

inline void *ma_memcpy(void *dest, const void *src, size_t n)
{
	return MA_LIBC_PREFIX(memcpy)(dest, src, n);
}

inline void *ma_memset(void *dest, int c, size_t n)
{
	return MA_LIBC_PREFIX(memset)(dest, c, n);
}
#endif

#endif
