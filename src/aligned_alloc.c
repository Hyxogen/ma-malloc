#include "ma/internal.h"

#include <errno.h>

void *ma_aligned_alloc(size_t align, size_t size)
{
	(void) align;
	(void) size;
#if MA_SEGREGATED_BESTFIT
	//it would probably be possible, but I can't be bothered to implement
	//this, it is supported if !MA_SEGREGATED_BESTFIT
	errno = ENOTSUP;
	return NULL;
#endif
	//TODO
	errno = ENOTSUP;
	return NULL;
}
