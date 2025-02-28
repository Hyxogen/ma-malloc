#ifndef MA_LIBC_INTERNAL_H
#define MA_LIBC_INTERNAL_H

#ifndef MA_USE_LIBFT
#define MA_USE_LIBFT 0
#endif

#if MA_USE_LIBFT
#include <ft/string.h>
#define MA_LIBC_PREFIX(x) ft_##x
#else
#define MA_LIBC_PREFIX(x) x
#endif

#endif
