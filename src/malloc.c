#include "ma/internal.h"

static struct ma_hdr *ma_malloc_from_bins(struct ma_arena *arena, size_t n)
{
	struct ma_hdr **list;
	struct ma_hdr *chunk = ma_find_in_bins(arena, n, &list);

	if (!chunk)
		return NULL;
	ft_assert(list);

	ma_unlink_chunk(list, chunk);
	ma_maybe_split(arena, chunk, n);
	return chunk;
}

#if MA_SEGREGATED_BESTFIT
static size_t ma_freelist_idx_from_size(size_t size)
{
	if (size <= MA_MAX_SMALL_SIZE)
		return 0;
	ft_assert(size <= MA_MAX_LARGE_SIZE);
	return 1;
}

static size_t ma_freelist_idx(const struct ma_hdr *hdr)
{
	if (ma_is_small(hdr))
		return 0;
	ft_assert(ma_is_large(hdr));
	return 1;
}
#endif

static struct ma_hdr *ma_malloc_from_freelists(struct ma_arena *arena, size_t n)
{
#if MA_SEGREGATED_BESTFIT
	size_t idx = ma_freelist_idx_from_size(n);
	struct ma_hdr **list = &arena->tops[idx];
#else
	struct ma_hdr **list = &arena->tops[0];
#endif

	struct ma_hdr *chunk = ma_find_bestfit(*list, n);
	if (chunk) {
		ma_unlink_chunk(list, chunk);
		ma_maybe_split(arena, chunk, n);
	}
	return chunk;
}

static struct ma_hdr *ma_malloc_from_new_chunk(struct ma_arena *arena, size_t n)
{
	enum ma_size_class class = ma_get_size_class_from_size(n);
	size_t alloc_size = ma_get_prealloc_size(class);

	struct ma_hdr *chunk = ma_alloc_chunk(arena, alloc_size, class);
	if (!chunk)
		return NULL;

	ft_assert(ma_should_split(chunk, n));

	struct ma_hdr *rem = ma_split_chunk(chunk, n);
	ma_append_chunk_any(arena, rem);
	return chunk;
}

static struct ma_hdr *ma_malloc_common(struct ma_arena *arena, size_t n)
{
	n = ma_pad_requestsize(n);

	struct ma_hdr *chunk = ma_malloc_from_bins(arena, n);
	if (!chunk) {
		chunk = ma_malloc_from_freelists(arena, n);
		if (!chunk)
			chunk = ma_malloc_from_new_chunk(arena, n);
	}
	return chunk;
}

static void *ma_malloc_no_lock(struct ma_arena *arena, size_t n)
{
	if (!ma_check_requestsize(n))
		return NULL;

	struct ma_hdr *chunk = NULL;

	if (n <= MA_MAX_LARGE_SIZE)
		chunk = ma_malloc_common(arena, n);
	else
		chunk = ma_alloc_chunk(arena, n, MA_HUGE);

	if (chunk) {
		ma_set_inuse(chunk, true);
		return ma_chunk_to_mem(chunk);
	}
	return NULL;
}

void *ma_malloc(size_t n)
{
	ma_dump_print("//ma_malloc(%zu);\n", n);

	ma_maybe_initialize();

	if (!ma_check_requestsize(n))
		return NULL;

	struct ma_arena *arena = ma_get_current_arena();
	ma_lock_arena(arena);

	ma_assert_correct_arena(arena);
	void *p = ma_malloc_no_lock(arena, n);
	ma_assert_correct_arena(arena);

	ma_unlock_arena(arena);

	ft_assert(!p || MA_IS_ALIGNED_TO(p, MA_MALLOC_ALIGN));

	ma_dump_print("void *tmp_%p = ma_malloc(%zu);\n", p, n);
	return p;
}
