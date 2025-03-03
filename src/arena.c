#include "ma/internal.h"

#include <ma/libc/assert.h>
#include <ma/libc/stdlib.h>
#include <ma/libc/string.h>

void ma_init_arena(struct ma_arena *arena)
{
	int rc;
	if ((rc = ma_init_mutex(&arena->mtx))) {
		eprint("ma_init_mutex: %s\n", ma_strerror(rc));
		ma_abort();
	}

	ma_init_debug(&arena->debug);
}

void ma_lock_arena(struct ma_arena *arena)
{
	int rc;
	if ((rc = ma_lock_mutex(&arena->mtx))) {
		eprint("ma_lock_mutex: %s\n", ma_strerror(rc));
		ma_abort();
	}
}

void ma_unlock_arena(struct ma_arena *arena)
{
	int rc;
	if ((rc = ma_unlock_mutex(&arena->mtx))) {
		eprint("ma_unlock_mutex: %s\n", ma_strerror(rc));
		ma_abort();
	}
}

size_t ma_small_binidx(size_t size)
{
	ma_assert(size >= MA_MIN_ALLOC_SIZE);
	size -= MA_MIN_ALLOC_SIZE;
	ma_assert(MA_IS_MULTIPLE_OF(size, MA_SMALLBIN_STEP));
	return size / MA_SMALLBIN_STEP;
}

size_t ma_freelist_idx_from_size(size_t size)
{
#if MA_SEGREGATED_BESTFIT
	if (size <= MA_MAX_SMALL_SIZE)
		return 0;
	ma_assert(size <= MA_MAX_LARGE_SIZE);
	return 1;
#else
	(void)size;
	return 0;
#endif
}

size_t ma_freelist_idx(const struct ma_hdr *hdr)
{
#if MA_SEGREGATED_BESTFIT
	if (ma_is_small(hdr))
		return 0;
	ma_assert(ma_is_large(hdr));
	return 1;
#else
	(void)hdr;
	return 0;
#endif
}

size_t ma_large_binidx(size_t n)
{
	ma_assert(n >= MA_MIN_LARGE_SIZE);
	n -= MA_MIN_LARGE_SIZE;

	// TODO use macros for the magic values
	size_t count = 32;
	size_t size = 64;
	size_t offset = MA_SMALLBIN_COUNT;
	while (count >= 2) {
		if (n <= count * size)
			return offset + n / size;

		n -= count * size;
		offset += count;
		count /= 2;
		size *= MA_LARGE_MULTIPLIER;
	}
	return MA_BIN_COUNT - 1;
}

size_t ma_binidx(size_t size)
{
	ma_assert(size >= MA_MIN_SMALL_SIZE);
	if (size <= MA_MAX_SMALL_SIZE)
		return ma_small_binidx(size);
	ma_assert(size >= MA_MIN_LARGE_SIZE);
	ma_assert(size <= MA_MAX_LARGE_SIZE);
	return ma_large_binidx(size);
}

bool ma_is_binable(const struct ma_hdr *chunk)
{
	size_t size = ma_get_size(chunk);
#if MA_SEGREGATED_BESTFIT
	switch (ma_get_size_class(chunk)) {
	case MA_SMALL:
		return size >= MA_MIN_SMALL_SIZE && size <= MA_MAX_SMALL_SIZE;
	case MA_LARGE:
		return size >= MA_MIN_LARGE_SIZE && size <= MA_MAX_LARGE_SIZE;
	default:
		return false;
	}
#else
	// TODO the min check can probably be removed if the logic is correct
	return size >= MA_MIN_SMALL_SIZE && size <= MA_MAX_LARGE_SIZE;
#endif
}

static struct ma_hdr **ma_get_list(struct ma_arena *arena,
				   const struct ma_hdr *chunk,
				   size_t *selected_bin)
{
	ma_assert(!ma_is_huge(chunk));
	size_t size = ma_get_size(chunk);

	struct ma_hdr **list = NULL;

#if MA_SEGREGATED_BESTFIT

	if (ma_is_small(chunk)) {
		if (size <= MA_MAX_SMALL_SIZE) {
			ma_assert(size >= MA_MIN_SMALL_SIZE);
			size_t bin = ma_small_binidx(size);
			list = &arena->bins[bin];
			*selected_bin = bin;
		} else {
			list = &arena->tops[0];
		}
	} else {
		if (size <= MA_MAX_LARGE_SIZE) {
			ma_assert(size >= MA_MIN_LARGE_SIZE);
			size_t bin = ma_large_binidx(size);
			list = &arena->bins[bin];
			*selected_bin = bin;
		} else {
			list = &arena->tops[1];
		}
	}
#else
	if (ma_is_binable(chunk)) {
		size_t bin = ma_binidx(size);
		list = &arena->bins[bin];
		*selected_bin = bin;
	} else {
		list = &arena->tops[0];
	}
#endif

	return list;
}

void ma_clear_bin(struct ma_arena *arena, size_t idx)
{
	ma_assert(idx < MA_BIN_COUNT);

	size_t offset = idx % MA_BINMAPS_PER_ENTRY;
	arena->bin_maps[idx / MA_BINMAPS_PER_ENTRY] ^= 1ull << offset;
}

void ma_mark_bin(struct ma_arena *arena, size_t idx)
{
	ma_assert(idx < MA_BIN_COUNT);

	size_t offset = idx % MA_SMALLBIN_COUNT;
	arena->bin_maps[idx / MA_BINMAPS_PER_ENTRY] |= 1ull << offset;
}

struct ma_hdr *ma_find_in_bins(struct ma_arena *arena, size_t n,
			       struct ma_hdr ***from)
{
	size_t max;
	size_t bin;

#if MA_SEGREGATED_BESTFIT
	if (ma_get_size_class_from_size(n) == MA_SMALL) {
		bin = ma_small_binidx(n);
		max = MA_SMALLBIN_COUNT;
	} else {
		bin = ma_large_binidx(n);
		max = MA_BIN_COUNT;
	}
#else
	bin = ma_binidx(n);
	max = MA_BIN_COUNT;
#endif

	while (bin < max) {
		uint64_t mask = ~((1ull << (bin % MA_BINMAPS_PER_ENTRY)) - 1);
		uint64_t bins =
		    mask & arena->bin_maps[bin / MA_BINMAPS_PER_ENTRY];

		if (bins) {
			uint64_t i = ma_ctlz(bins);

			struct ma_hdr **list = &arena->bins[i];

			struct ma_hdr *chunk = ma_find_bestfit(*list, n);
			if (chunk) {
				*from = list;
				return chunk;
			}

			if (!*list)
				ma_clear_bin(arena, bin);

			bin += 1;
		} else {
			bin = MA_ALIGN_UP(bin + 1, MA_BINMAPS_PER_ENTRY);
		}
	}
	return NULL;
}

void ma_append_chunk_any(struct ma_arena *arena, struct ma_hdr *chunk)
{
	size_t bin = -1;
	struct ma_hdr **list = ma_get_list(arena, chunk, &bin);

	// we could also find the bin index by just searching where in the bin
	// array the list is (if it is a bin)
	if (ma_is_binable(chunk))
		ma_mark_bin(arena, bin);
	ma_append_chunk(list, chunk);
}

void ma_unlink_chunk_any(struct ma_arena *arena, struct ma_hdr *chunk)
{
	if (ma_is_huge(chunk))
		return; // nothing to unlink
	size_t tmp;
	struct ma_hdr **list = ma_get_list(arena, chunk, &tmp);

	ma_unlink_chunk(list, chunk);
}

void ma_dump_arena(const struct ma_arena *arena)
{
	ma_debug_for_each(&arena->debug, ma_dump_all_chunks, NULL);
}

#if !defined(FT_NDEBUG) && MA_CHECK_SELF
void ma_assert_correct_bin(const struct ma_hdr *list, size_t min, size_t max)
{
	const struct ma_hdr *cur = list;
	if (!cur)
		return;

	do {
		ma_assert(!ma_is_inuse(cur));

		size_t size = ma_get_size(cur);
		ma_assert(size >= min);
		ma_assert(size <= max);

		ma_assert(cur->next);
		ma_assert(cur->prev);

		ma_assert(cur->next->prev == cur);
		ma_assert(cur->prev->next == cur);

		cur = cur->next;
	} while (cur != list);
}

void ma_assert_correct_arena(const struct ma_arena *arena)
{
	ma_debug_for_each(&arena->debug, ma_assert_correct_all_chunks, NULL);

	size_t size = MA_MIN_SMALL_SIZE;
	size_t bin;

	for (bin = 0; bin < MA_SMALLBIN_COUNT; ++bin) {
		ma_assert_correct_bin(arena->bins[bin], size, size);
		size += MA_SMALLBIN_STEP;
	}

	size_t min_size = MA_MIN_LARGE_SIZE;
	size_t max_size;
	size_t count = 32;
	size = 64;

	while (count >= 2) {
		for (size_t i = 0; i < count; ++i) {
			max_size = min_size + size;
			ma_assert_correct_bin(arena->bins[bin + i], min_size,
					      max_size);
			min_size = max_size;
		}
		bin += count;
		count /= 2;
		size *= 8;
	}
}
#endif
