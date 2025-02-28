#ifndef MA_SYSDEPS_H
#define MA_SYSDEPS_H

#include <stddef.h>
#include <stdbool.h>

#define MA_SYSALLOC_FAILED ((void *)-1)

int ma_sysalloc_granularity(void);
void *ma_sysalloc(size_t size);
bool ma_sysfree(void *p, size_t size);

#endif
