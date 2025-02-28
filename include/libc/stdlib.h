#ifndef LIBC_STDLIB_H
#define LIBC_STDLIB_H

#include <libc/internal.h>

#if MA_USE_LIBFT
#include <ft/stdlib.h>
#else
#include <stdlib.h>
#endif

#if MA_USE_LIBFT
/* Libft has no ft_abort (since we cannot really make one with the usual allowed
 * functions */
[[noreturn]]
void ma_abort(void);
#else
[[noreturn]]
inline void ma_abort(void)
{
	LIBC_PREFIX(abort)();
}
#endif

inline char *ma_getenv(const char *name) { return LIBC_PREFIX(getenv)(name); }

inline unsigned long long ma_strtoull(const char *restrict nptr,
				      char **restrict endptr, int base)
{
	return LIBC_PREFIX(strtoull)(nptr, endptr, base);
}

#endif
