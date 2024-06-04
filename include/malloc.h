#ifndef FT_MALLOC_H
#define FT_MALLOC_H

#include <stddef.h>

void *ft_malloc(size_t n);
void ft_free(void *p);
void *ft_calloc(size_t nmemb, size_t size);
void *ft_realloc(void *p, size_t size);

void *ft_aligned_alloc(size_t align, size_t size);
size_t ft_malloc_usable_size(void *p);
void *ft_memalign(size_t align, size_t size);
void *ft_valloc(size_t size);
void *ft_pvalloc(size_t size);

void show_alloc_mem(void);

#endif
