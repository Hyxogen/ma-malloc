#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

#include <libc/internal.h>

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
	int rc = LIBC_PREFIX(vprintf)(format, args);
	va_end(args);
	return rc;
}

static inline int ma_dprintf(int fd, const char *restrict format, ...)
{
	va_list args;

	va_start(args, format);
	int rc = LIBC_PREFIX(vdprintf)(fd, format, args);
	va_end(args);
	return rc;
}

#endif
