#include "ma/internal.h"

void ma_init_arena(struct ma_arena *arena)
{
	int rc;
	if ((rc = pthread_mutex_init(&arena->mtx, NULL))) {
		eprint("pthread_mutex_init: %s\n", ft_strerror(rc));
		ft_abort();
	}
}

void ma_lock_arena(struct ma_arena *arena)
{
	int rc;
	if ((rc = pthread_mutex_lock(&arena->mtx))) {
		eprint("pthread_mutex_lock: %s\n", ft_strerror(rc));
		ft_abort();
	}
}

void ma_unlock_arena(struct ma_arena *arena)
{
	int rc;
	if ((rc = pthread_mutex_unlock(&arena->mtx))) {
		eprint("pthread_mutex_unlock: %s\n", ft_strerror(rc));
		ft_abort();
	}
}

size_t ma_small_binidx(size_t size)
{
	ft_assert(size >= MA_MIN_ALLOC_SIZE);
	size -= MA_MIN_ALLOC_SIZE;
	ft_assert(MA_IS_MULTIPLE_OF(size, MA_SMALLBIN_STEP));
	return size / MA_SMALLBIN_STEP;
}

size_t ma_large_binidx(size_t n)
{
	ft_assert(n >= MA_MIN_LARGE_SIZE);
	n -= MA_MIN_LARGE_SIZE;

	//TODO use macros for the magic values
	size_t count = 32;
	size_t size = 64;
	size_t offset = MA_SMALLBIN_COUNT;
	while (count >= 2) {
		if (n <= count * size)
			return offset + n / size;

		n -= count * size;
		offset += count;
		count /= 2;
		size *= 8;
	}
	return MA_BIN_COUNT - 1;
}

size_t ma_binidx(size_t size)
{
	ft_assert(size >= MA_MIN_SMALL_SIZE);
	if (size <= MA_MAX_SMALL_SIZE)
		return ma_small_binidx(size);
	ft_assert(size >= MA_MIN_LARGE_SIZE);
	ft_assert(size <= MA_MAX_LARGE_SIZE);
	return ma_large_binidx(size);
}

bool ma_is_binable(const struct ma_hdr *chunk)
{
	size_t size = ma_get_size(chunk);
	//TODO the min check can probably be removed if the logic is correct
	return size >= MA_MIN_SMALL_SIZE && size <= MA_MAX_LARGE_SIZE;
}

static struct ma_hdr **ma_get_list(struct ma_arena *arena,
				   const struct ma_hdr *chunk,
				   size_t *selected_bin)
{
	ft_assert(!ma_is_huge(chunk));
	size_t size = ma_get_size(chunk);

	struct ma_hdr **list = NULL;

#if MA_SEGREGATED_BESTFIT

	if (ma_is_small(chunk)) {
		if (size <= MA_MAX_SMALL_SIZE) {
			ft_assert(size >= MA_MIN_SMALL_SIZE);
			size_t bin = ma_small_binidx(size);
			list = &arena.bins[bin];
			*selected_bin = bin;
		} else {
			list = &arena->tops[0];
		}
	} else {
		if (size <= MA_MAX_LARGE_SIZE) {
			ft_assert(size >= MA_MIN_LARGE_SIZE);
			size_t bin = ma_large_binidx(size);
			list = &arena.bins[bin];
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
	ft_assert(idx < MA_BIN_COUNT);

	size_t offset = idx % MA_BINMAPS_PER_ENTRY;
	arena->bin_maps[idx / MA_BINMAPS_PER_ENTRY] ^= 1ull << offset;
}

void ma_mark_bin(struct ma_arena *arena, size_t idx)
{
	ft_assert(idx < MA_BIN_COUNT);

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
	size_t tmp;
	struct ma_hdr **list = ma_get_list(arena, chunk, &tmp);
	
	ma_unlink_chunk(list, chunk);
}

void ma_dump_arena(const struct ma_arena *arena)
{
	eprint("SMALL:\n");
	ma_dump_all_chunks(arena->debug[0]);
	eprint("LARGE:\n");
	ma_dump_all_chunks(arena->debug[1]);
}

#ifndef FT_NDEBUG

void ma_assert_correct_bin(const struct ma_hdr *list, size_t min, size_t max)
{
	const struct ma_hdr *cur = list;
	if (!cur)
		return;

	do {
		ft_assert(!ma_is_inuse(cur));

		size_t size = ma_get_size(cur);
		ft_assert(size >= min);
		ft_assert(size <= max);

		ft_assert(cur->next);
		ft_assert(cur->prev);

		ft_assert(cur->next->prev == cur);
		ft_assert(cur->prev->next == cur);

		cur = cur->next;
	} while (cur != list);
}

void ma_assert_correct_arena(const struct ma_arena *arena)
{
	ma_assert_correct_all_chunks(arena->debug[0]);
	ma_assert_correct_all_chunks(arena->debug[1]);

	size_t size = MA_MIN_SMALL_SIZE;

	for (int i = 0; i < MA_SMALLBIN_COUNT; ++i) {
		ma_assert_correct_bin(arena->bins[i], size, size);
		size += MA_SMALLBIN_STEP;
	}
}
#endif
