#ifndef MA_LIBC_STDIO_H
#define MA_LIBC_STDIO_H

#include <ma/libc/internal.h>

#if MA_USE_LIBFT
#include <ft/stdio.h>
#else
#include <stdio.h>
#endif

#include <stdarg.h>

static inline int ma_printf(const char *restrict format, ...)
{
	va_list args;

	va_start(args, format);
	int rc = MA_LIBC_PREFIX(vprintf)(format, args);
	va_end(args);
	return rc;
}

static inline int ma_dprintf(int fd, const char *restrict format, ...)
{
	va_list args;

	va_start(args, format);
	int rc = MA_LIBC_PREFIX(vdprintf)(fd, format, args);
	va_end(args);
	return rc;
}

#endif
