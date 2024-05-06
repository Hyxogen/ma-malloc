#include "malloc.h"
#include <malloc.h>

int main()
{
	size_t size = 16384 * 2;

	printf("%p\n", ft_malloc(size));
	printf("%p\n", ft_malloc(size));
	show_alloc_mem();
	return 0;
}
