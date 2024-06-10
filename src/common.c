#include "ma/internal.h"

#include <errno.h>

size_t ma_get_prealloc_size(enum ma_size_class class)
{
#if MA_SEGREGATED_BESTFIT
	if (class == MA_SMALL)
		return MA_MAX_SMALL_SIZE * MA_CHUNKS_PER_ZONE;
	ft_assert(class == MA_LARGE);
	return MA_MAX_LARGE_SIZE * MA_CHUNKS_PER_ZONE;
#else
	(void)class;
	return MA_MAX_LARGE_SIZE * MA_CHUNKS_PER_ZONE;
#endif
}

size_t ma_pad_requestsize(size_t size)
{
	if (size < MA_MIN_ALLOC_SIZE)
		size = MA_MIN_ALLOC_SIZE;
	size = MA_ALIGN_UP(size, MA_HALF_MALLOC_ALIGN) | MA_HALF_MALLOC_ALIGN;
	ft_assert(size & MA_HALF_MALLOC_ALIGN);
	return size;
}

void ma_check_pointer(void *p)
{
	if (!MA_IS_ALIGNED_TO(p, MA_MALLOC_ALIGN)) {
		eprint("free(): invalid pointer\n");
		ft_abort();
	}
}

bool ma_check_requestsize(size_t size)
{
	if (size > PTRDIFF_MAX) {
		errno = ENOMEM;
		return false;
	}

#if !MA_GLIBC_COMPATIBLE
	if (!size)
		return false;
#endif
	return true;
}

uint64_t ma_ctlz(uint64_t x) {
#if FT_BONUS
	return __builtin_ctzll(x);
#else
	if (x) {
		x = (x ^ (x - 1)) >> 1;

		int c;
		for (c = 0; x; c++) {
			x >>= 1;
		}

		return c;
	}
	return CHAR_BIT * sizeof(x);
#endif
}

enum ma_size_class ma_get_size_class_from_size(size_t size)
{
	if (size <= MA_MAX_SMALL_SIZE)
		return MA_SMALL;
	if (size <= MA_MAX_LARGE_SIZE)
		return MA_LARGE;
	return MA_HUGE;
}
