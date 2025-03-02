#ifndef MA_MALLOC_H
#define MA_MALLOC_H

/* This header provides the libc prototypes for memory allocator functions */
/* You'll have to compile ma-malloc with MA_COMPILE_AS_LIBC=1 to use these */

#include <stddef.h>

void *malloc(size_t n);
void free(void *p);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *p, size_t size);

void *aligned_alloc(size_t align, size_t size);
size_t malloc_usable_size(void *p);
void *memalign(size_t align, size_t size);
void *valloc(size_t size);
void *pvalloc(size_t size);

#endif
