#include "ma/internal.h"

#include <ft/string.h>

void *ma_calloc(size_t nmemb, size_t size)
{
	ma_dump_print("//ma_calloc(%zu, %zu);\n", nmemb, size);
	size_t total = nmemb * size;

	if (nmemb && ((total / nmemb) != size))
		return NULL;

	if (!ma_check_requestsize(total))
		return NULL;

	void *p = ma_malloc(total);

	if (p) {
		struct ma_hdr *chunk = ma_mem_to_chunk(p);

		// watch out, the chunk might have been perturbed, so we don't
		// know for sure if it has been zeroed
		ft_memset(p, 0, ma_get_size(chunk));
	}

	ma_dump_print("void *tmp_%p = ma_calloc(%zu, %zu);\n", p, nmemb, size);
	return p;
}
