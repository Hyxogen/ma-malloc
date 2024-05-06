#include "malloc.h"
#include <assert.h>
#include <malloc.h>

int main()
{
	void *tmp = ft_malloc(256);

	show_alloc_mem();

	ft_free(tmp);

	tmp = ft_malloc(256);
	tmp = ft_malloc(256);
	show_alloc_mem();

	show_alloc_mem();
	return 0;
}
