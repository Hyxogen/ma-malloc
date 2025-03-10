#ifndef MA_LIBC_STDLIB_H
#define MA_LIBC_STDLIB_H

#include <ma/libc/internal.h>

#if MA_USE_LIBFT
#include <ft/stdlib.h>
#elif !MA_PLATFORM_BARE
#include <stdlib.h>
#endif

#if MA_USE_LIBFT
/* Libft has no ft_abort (since we cannot really make one with the usual allowed
 * functions */
[[noreturn]]
void ma_abort(void);
#elif MA_PLATFORM_BARE
[[noreturn]]
void ma_abort(void);
char *ma_getenv(const char *name);
unsigned long long ma_strtoull(const char *restrict nptr, char **restrict endptr, int base);

#else
[[noreturn]]
inline void ma_abort(void)
{
	MA_LIBC_PREFIX(abort)();
}


inline char *ma_getenv(const char *name) { return MA_LIBC_PREFIX(getenv)(name); }

inline unsigned long long ma_strtoull(const char *restrict nptr,
				      char **restrict endptr, int base)
{
	return MA_LIBC_PREFIX(strtoull)(nptr, endptr, base);
}
#endif

#endif
