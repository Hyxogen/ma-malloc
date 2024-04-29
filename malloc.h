#ifndef FT_MALLOC_H
#define FT_MALLOC_H

#include <stddef.h>

void *ft_malloc(size_t n);
void ft_free(void *p);
void show_alloc_mem(void);

#endif
