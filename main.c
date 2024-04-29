#include "malloc.h"
#include <assert.h>
#include <malloc.h>

int main()
{
	void *tmp = ft_malloc(1);

	show_alloc_mem();

	ft_free(tmp);
	ft_free(tmp);
}
