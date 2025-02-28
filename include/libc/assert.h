#ifndef LIBC_ASSERT_H
#define LIBC_ASSERT_H

#if MA_USE_LIBFT
/* Libft doesn't have a ft_assert implementation for the same reasons it doesn't
 * have a ft_abort */

#include <libc/stdio.h>
#include <libc/stdlib.h>

static inline void ma_assert_impl(int c, const char *pred, const char *file,
				  const char *func, int line)
{

	if (!c) {
		ma_printf("%s:%i: %s: Assertion '%s` failed.\n", file, line,
			  func, pred);
		ma_abort();
	}
}

#define ma_assert(c)                                                           \
	ma_assert_impl((c) != 0, #c, __FILE__, __FUNCTION__, __LINE__)

#else
#include <assert.h>
#define ma_assert(x) assert(x)
#endif

#endif
