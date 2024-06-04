#include "ma/internal.h"

#include <ft/string.h>

//TODO in place growing
void *ma_realloc(void *p, size_t newsize)
{
	if (!p)
		return ma_malloc(newsize);

#if MA_GLIBC_COMPATIBLE
	if (!newsize) {
		ma_free(p);
		return NULL;
	}
#else
	//resize to null is undefined behaviour in C23
	ft_assert(newsize);
#endif

	if (!ma_check_requestsize(newsize))
		return NULL;

	ma_dump_print("//ma_realloc(tmp_%p, %zu);\n", p, newsize);

	struct ma_arena *arena = ma_get_current_arena();
	ma_lock_arena(arena);

	void *newp = ma_malloc_no_lock(arena, newsize);
	if (newp) {
		struct ma_hdr *chunk = ma_mem_to_chunk(p);
		size_t chunk_size = ma_get_size(chunk);

		ft_memcpy(newp, p, chunk_size < newsize ? chunk_size : newsize);
		ma_free_no_lock(arena, p);
	}

	ma_unlock_arena(arena);

	ma_dump_print("void *tmp_%p = ma_realloc(tmp_%p, %zu);\n", newp, p, newsize);

	return newp;
}
