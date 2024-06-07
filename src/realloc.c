#include "ma/internal.h"

#include <ft/string.h>

static void *ma_realloc_slow(struct ma_arena *arena, struct ma_hdr *old_chunk,
			     size_t new_size)
{
	void *newp = ma_malloc_no_lock(arena, new_size);
	if (newp) {
		void *oldp = ma_chunk_to_mem(old_chunk);
		size_t old_size = ma_get_size(old_chunk);

		ft_memcpy(newp, oldp,
			  old_size < new_size ? old_size : new_size);
		ma_free_no_lock(arena, oldp);
	}
	return newp;
}

static void *ma_realloc_shrink(struct ma_arena *arena, struct ma_hdr *old_chunk,
			       size_t new_size)
{
	if (ma_is_huge(old_chunk)) {
		// TODO it's probably better to try to unmap some stuff
		return ma_chunk_to_mem(old_chunk);
	}

	size_t old_size = ma_get_size(old_chunk);
	ft_assert(old_size >= new_size);

	size_t rem = old_size - new_size;

	if (rem >= MA_MIN_CHUNK_SIZE &&
	    (!MA_SEGREGATED_BESTFIT || ma_should_split(old_chunk, new_size))) {
		ma_set_size(old_chunk, new_size);

		struct ma_hdr *next = ma_next_hdr(old_chunk);
		struct ma_hdr *nextnext =
		    ma_init_chunk(next, ma_get_size_class(old_chunk),
				  rem - MA_HEADER_SIZE, true);

		// TODO just use ma_maybe_merge?
		if (!ma_is_sentinel(nextnext) && !ma_is_inuse(nextnext)) {
			ma_unlink_chunk_any(arena, nextnext);
			next = ma_merge_chunks(next, nextnext);
		}
		ma_append_chunk_any(arena, next);
	}
	return ma_chunk_to_mem(old_chunk);
}

static void *ma_realloc_grow(struct ma_arena *arena, struct ma_hdr *old_chunk,
			     size_t new_size)
{
	size_t old_size = ma_get_size(old_chunk);

	ft_assert(old_size <= new_size);

	size_t needed = ma_pad_requestsize(new_size - old_size);
	enum ma_size_class chunk_class = ma_get_size_class(old_chunk);

	struct ma_hdr *next = ma_next_hdr(old_chunk);
	// Wow this is ugly, TODO make it prettier
	if (ma_is_sentinel(next) || ma_is_inuse(next) ||
	    ma_get_size(next) + MA_HEADER_SIZE < needed ||
	    (MA_SEGREGATED_BESTFIT && !ma_should_split(next, needed) &&
	     chunk_class !=
		 ma_get_size_class_from_size(old_size + ma_get_size(next))))
		return ma_realloc_slow(arena, old_chunk, new_size);

	ma_unlink_chunk_any(arena, next);
	if (ma_should_split(next, needed)) {
		ma_append_chunk_any(arena, ma_split_chunk(next, needed));
		ma_set_size(old_chunk, old_size + needed + MA_HEADER_SIZE);
	} else {
		ma_set_size(old_chunk,
			    old_size + ma_get_size(next) + MA_HEADER_SIZE);
	}

	ma_set_pinuse(ma_next_hdr(old_chunk), true);
	return ma_chunk_to_mem(old_chunk);
}

static void *ma_realloc_no_lock(struct ma_arena *arena, void *p,
				size_t new_size)
{
	ft_assert(p);
	ft_assert(new_size > 0);

	new_size = ma_pad_requestsize(new_size);

	struct ma_hdr *chunk = ma_mem_to_chunk(p);

	enum ma_size_class request_class =
	    ma_get_size_class_from_size(new_size);
	enum ma_size_class chunk_class = ma_get_size_class(chunk);

	if (!MA_SEGREGATED_BESTFIT || request_class == chunk_class) {
		size_t old_size = ma_get_size(chunk);

		if (new_size < old_size) {
			return ma_realloc_shrink(arena, chunk, new_size);
		} else if (new_size > old_size) {
			return ma_realloc_grow(arena, chunk, new_size);
		}
		return p;
	}
	return ma_realloc_slow(arena, chunk, new_size);
}

// TODO in place growing
void *ma_realloc(void *p, size_t new_size)
{
	if (!p)
		return ma_malloc(new_size);

#if MA_GLIBC_COMPATIBLE
	if (!new_size) {
		ma_free(p);
		return NULL;
	}
#else
	// resize to null is undefined behaviour in C23
	ft_assert(new_size);
#endif

	if (!ma_check_requestsize(new_size))
		return NULL;

	ma_dump_print("//ma_realloc(tmp_%p, %zu);\n", p, new_size);

	struct ma_arena *arena = ma_get_current_arena();
	ma_lock_arena(arena);

	void *newp = ma_realloc_no_lock(arena, p, new_size);

	ma_unlock_arena(arena);

	ma_dump_print("void *tmp_%p = ma_realloc(tmp_%p, %zu);\n", newp, p,
		      new_size);

	return newp;
}
