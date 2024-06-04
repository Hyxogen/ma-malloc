#include "ma/internal.h"

static void *ma_aligned_alloc_no_lock(struct ma_arena *arena, size_t align, size_t size)
{
	ft_assert(align > MA_MALLOC_ALIGN);

	if (!ma_check_requestsize(size))
		return NULL;

	void *p = ma_malloc_no_lock(arena, size + 2 * align - 1);
	if (!p)
		return NULL;

	void *alignedp = (void*) MA_ALIGN_UP((uintptr_t) p, align);

	if (p != alignedp) {
		size_t diff = alignedp - p;

		if (diff < MA_MIN_CHUNK_SIZE) {
			alignedp = (void*) MA_ALIGN_UP((uintptr_t) alignedp + 1, align);
			diff = alignedp - p;
		}

		ft_assert(diff >= MA_MIN_CHUNK_SIZE);

		struct ma_hdr *chunk = ma_mem_to_chunk(p);

		ma_set_inuse(chunk, false);
		struct ma_hdr *aligned_chunk = ma_split_chunk(chunk, diff - MA_HEADER_SIZE);
		ma_set_inuse(aligned_chunk, true);

		ft_assert(ma_get_size(aligned_chunk) >= size);
		ft_assert(ma_chunk_to_mem(aligned_chunk) == alignedp);

		if (!ma_is_huge(chunk))
			ma_append_chunk_any(arena, chunk);
	}

	return alignedp;
}

void *ma_aligned_alloc(size_t align, size_t size)
{
#if MA_SEGREGATED_BESTFIT
	//it would probably be possible, but I can't be bothered to implement
	//this, it is supported if !MA_SEGREGATED_BESTFIT
	errno = ENOTSUP;
	return NULL;
#endif
	if (align <= MA_MALLOC_ALIGN)
		return ma_malloc(size);

	ma_dump_print("//ma_aligned_alloc(%zu, %zu);\n", align, size);

	ma_maybe_initialize();

	if (!ma_check_requestsize(size))
		return NULL;

	struct ma_arena *arena = ma_get_current_arena();
	ma_lock_arena(arena);

	void *p = ma_aligned_alloc_no_lock(arena, align, size);

	ma_unlock_arena(arena);

	ma_dump_print("void *tmp_%p = ma_aligned_alloc(%zu, %zu);\n", p, align, size);
	ft_assert(!p || MA_IS_ALIGNED_TO(p, align));
	return p;
}
