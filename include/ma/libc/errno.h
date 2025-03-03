#ifndef MA_LIBC_ERRNO_H
#define MA_LIBC_ERRNO_H

#if MA_PLATFORM_BARE
#define ENOMEM 12
#define EINVAL 22
#define ERANGE 34
#define ENOTSUP 95
#define errno 0
#define MA_SET_ERRNO(x) ((void) (x))
#else
#include <errno.h>
#define MA_ERRNO errno
#define MA_SET_ERRNO(x)                                                        \
	do {                                                                   \
		errno = (x);                                                   \
	} while (0)
#endif

#endif
