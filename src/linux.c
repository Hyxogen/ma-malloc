#include "ma/internal.h"

#include <unistd.h>
#include <sys/mman.h>

int ma_sysalloc_granularity(void)
{
	static int pagesize = -1;

	if (pagesize == -1)
		pagesize = getpagesize();
	return pagesize;
}

void *ma_sysalloc(size_t size)
{
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return MA_SYSALLOC_FAILED;
	return p;
}

bool ma_sysfree(void *p, size_t size)
{
	return munmap(p, size) == 0;
}
