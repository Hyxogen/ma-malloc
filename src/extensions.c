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

	// TODO should be pretty easy to implement
	errno = ENOTSUP;
	ft_assert(0);
	return NULL;
}

int ma_posix_memalign(void **memptr, size_t align, size_t size)
{
	if (align < sizeof(void *) || !MA_IS_POWER_OF_TWO_OR_ZERO(align))
		return EINVAL;

	int prev_errno = errno;

	if (size < MA_MIN_ALLOC_SIZE)
		size = MA_MIN_ALLOC_SIZE;

	void *p = ma_aligned_alloc(align, size);

	int res = 0;
	if (!p)
		res = ENOMEM;
	else
		*memptr = p;

	errno = prev_errno;
	return res;
}

void *ma_valloc(size_t size)
{
	(void)size;

	// TODO should be pretty easy to implement
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
