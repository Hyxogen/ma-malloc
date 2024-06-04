#include "ma/internal.h"

#include <errno.h>

size_t ma_malloc_usable_size(void *p)
{
	if (!p)
		return 0;

	ma_check_pointer(p);
	return ma_get_size(ma_mem_to_chunk(p));
}

void *ma_memalign(size_t align, size_t size)
{
	(void)align;
	(void)size;

	//TODO should be pretty easy to implement
	errno = ENOTSUP;
	ft_assert(0);
	return NULL;
}

void *ma_valloc(size_t size)
{
	(void)size;

	//TODO should be pretty easy to implement
	errno = ENOTSUP;
	ft_assert(0);
	return NULL;
}

void *ma_pvalloc(size_t size)
{
	(void)size;

	errno = ENOTSUP;
	ft_assert(0);
	return NULL;
}
