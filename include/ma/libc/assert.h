#ifndef MA_LIBC_ASSERT_H
#define MA_LIBC_ASSERT_H

#if MA_USE_LIBFT
/* Libft doesn't have a ft_assert implementation for the same reasons it doesn't
 * have a ft_abort */

#include <ma/libc/stdio.h>
#include <ma/libc/stdlib.h>

static inline void ma_assert_impl(int c, const char *pred, const char *file,
				  const char *func, int line)
{

	if (!c) {
		ma_printf("%s:%i: %s: Assertion '%s` failed.\n", file, line,
			  func, pred);
		ma_abort();
	}
}

#ifdef NDEBUG
#define ma_assert(c)
#else
#define ma_assert(c)                                                           \
	ma_assert_impl((c) != 0, #c, __FILE__, __FUNCTION__, __LINE__)
#endif

#elif MA_PLATFORM_BARE

_Noreturn void ma_assert_fail(const char *, const char *, int, const char *);

#define ma_assert(x)  ((void)((x) || (ma_assert_fail(#x, __FILE__, __LINE__, __func__),0)))
#else
#include <assert.h>
#define ma_assert(x) assert(x)
#endif

#endif
