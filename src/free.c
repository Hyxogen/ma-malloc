#include "ma/internal.h"

#include <errno.h>

static struct ma_hdr *ma_maybe_merge(struct ma_arena *arena,
				     struct ma_hdr *chunk)
{
	if (!ma_is_pinuse(chunk)) {
		struct ma_hdr *prev = ma_prev_hdr(chunk);
		ma_unlink_chunk_any(arena, prev);
		chunk = ma_merge_chunks(chunk, prev);
	}

	struct ma_hdr *next = ma_next_hdr(chunk);
	if (!ma_is_sentinel(next) && !ma_is_inuse(next)) {
		ma_unlink_chunk_any(arena, next);
		chunk = ma_merge_chunks(chunk, next);
	}

	return chunk;
}

static bool ma_should_unmap(const struct ma_arena *arena,
			    const struct ma_hdr *chunk)
{
	if (ma_is_huge(chunk))
		return true;
	if (!ma_is_sentinel(ma_next_hdr(chunk)))
		return false;

	if (!arena->tops[ma_freelist_idx(chunk)])
		return false;

	enum ma_size_class class = ma_get_size_class(chunk);

	size_t threshold = ma_get_prealloc_size(class);

	threshold += MA_CHUNK_ALLOC_PADDING;
	threshold = MA_ALIGN_UP(threshold, ma_sysalloc_granularity());
	threshold -= MA_CHUNK_ALLOC_PADDING;

	return ma_get_size(chunk) >= threshold;
}

static void ma_free_common(struct ma_arena *arena, struct ma_hdr *chunk)
{
	ma_set_inuse(chunk, false);

	chunk = ma_maybe_merge(arena, chunk);

	if (ma_should_unmap(arena, chunk))
		ma_dealloc_chunk(arena, chunk);
	else
		ma_append_chunk_any(arena, chunk);
}

void ma_free_no_lock(struct ma_arena *arena, void *p)
{
	ma_check_pointer(p);

	struct ma_hdr *chunk = ma_mem_to_chunk(p);
	ma_check_user_chunk(chunk);

	if (!ma_is_inuse(chunk)) {
		eprint("free(): %p: invalid pointer\n", p);
		ft_abort();
	}

	ma_free_common(arena, chunk);
}

void ma_free(void *p)
{
#if MA_GLIBC_COMPATIBLE
	// glibc requires that free() preserves errno
	int prev_errno = errno;
#endif
	if (!p)
		return;
	ma_dump_print("ma_free(tmp_%p);\n", p);

	ma_check_pointer(p);

	struct ma_arena *arena = ma_get_arena(p);
	ma_lock_arena(arena);

	ma_assert_correct_arena(arena);
	ma_free_no_lock(arena, p);
	ma_assert_correct_arena(arena);

	ma_unlock_arena(arena);

#if MA_GLIBC_COMPATIBLE
	errno = prev_errno;
#endif
}
