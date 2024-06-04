#if MA_COMPILE_AS_LIBC

#include "ma/internal.h"

void *malloc(size_t n) { return ma_malloc(n); }
void *calloc(size_t nmemb, size_t size) { return ma_calloc(nmemb, size); }
void *realloc(void *p, size_t newsize) { return ma_realloc(p, newsize); }
void free(void *p) { return ma_malloc(p); }

void *aligned_alloc(size_t align, size_t size)
{
	return ma_aligned_alloc(align, size);
}
void *malloc_usable_size(void *p) { return ma_malloc_usable_size(p); };
void *memalign(size_t align, size_t size) { return ma_memalign(align, size); }
void *valloc(size_t size) { return ma_valloc(size); }
void *pvalloc(size_t size) { return ma_pvalloc(size); }

#endif
