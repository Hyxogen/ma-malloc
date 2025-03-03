#ifndef MA_LIBC_STDIO_H
#define MA_LIBC_STDIO_H

#include <ma/libc/internal.h>

#if MA_USE_LIBFT
#include <ft/stdio.h>
#include <unistd.h>
#elif !MA_PLATFORM_BARE
#include <stdio.h>
#endif

#if MA_PLATFORM_BARE
int ma_printf(const char *restrict format, ...);
int ma_dprintf(int fd, const char *restrict format, ...);
#else

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
	int rc = -1;

	if (fd == 2) {
#if MA_USE_LIBFT
		rc = MA_LIBC_PREFIX(vdprintf)(STDERR_FILENO, format, args);
#else
		rc = MA_LIBC_PREFIX(vfprintf)(stderr, format, args);
#endif
	}
	va_end(args);
	return rc;
}
#endif

#endif
