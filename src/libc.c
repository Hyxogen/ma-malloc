#include "ma/internal.h"

#if MA_COMPILE_AS_LIBC

void *malloc(size_t n) { return ma_malloc(n); }
void *calloc(size_t nmemb, size_t size) { return ma_calloc(nmemb, size); }
void *realloc(void *p, size_t newsize) { return ma_realloc(p, newsize); }
void free(void *p) { ma_free(p); }

void *aligned_alloc(size_t align, size_t size)
{
	return ma_aligned_alloc(align, size);
}
size_t malloc_usable_size(void *p) { return ma_malloc_usable_size(p); };
void *memalign(size_t align, size_t size) { return ma_memalign(align, size); }
void *valloc(size_t size) { return ma_valloc(size); }
void *pvalloc(size_t size) { return ma_pvalloc(size); }
int posix_memalign(void **memptr, size_t align, size_t size)
{
	return ma_posix_memalign(memptr, align, size);
}

#endif
