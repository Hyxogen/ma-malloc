#ifndef FT_MALLOC_H
#define FT_MALLOC_H

#include <stddef.h>

void *ma_malloc(size_t n);
void ma_free(void *p);
void *ma_calloc(size_t nmemb, size_t size);
void *ma_realloc(void *p, size_t size);

void *ma_aligned_alloc(size_t align, size_t size);
size_t ma_malloc_usable_size(void *p);
void *ma_memalign(size_t align, size_t size);
void *ma_valloc(size_t size);
void *ma_pvalloc(size_t size);

void show_alloc_mem(void);
void show_alloc_mem_ex(void);

#endif
