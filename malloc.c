#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include <ft/stdio.h>

#define MALLOC_ALIGN (_Alignof(max_align_t))
#define HALF_MALLOC_ALIGN (MALLOC_ALIGN >> 1)
#define MIN_CHUNK_SIZE (4 * sizeof(size_t))
#define HEADER_SIZE_INUSE (sizeof(size_t))
#define FOOTER_SIZE (sizeof(size_t))
#define MIN_ALLOC_SIZE (MIN_CHUNK_SIZE - HEADER_SIZE_INUSE)
#define SMALLBIN_COUNT 64

#define MAX_SMALL_SIZE 1016

#define ROUND_UP(x, boundary) ((x + boundary - 1) & ~(boundary - 1))
#define IS_ALGINED_TO(x, boundary) (((uintmax_t)x & (boundary - 1)) == 0)

#ifdef NDEBUG
#define ft_assert(pred)
#else
#include <stdlib.h>
#define ft_assert(pred)                                                        \
	ft_assert_impl(!(!(pred)), #pred, __FUNCTION__, __FILE__, __LINE__)
#endif

#define eprint(...) ft_dprintf(STDERR_FILENO, __VA_ARGS__)

struct memhdr {
	size_t pinuse : 1;
	size_t cinuse : 1;
	size_t mapped : 1;
	size_t size : (sizeof(size_t) * 8) - 3;

	// only accesible if !cinuse
	struct memhdr *next;
	struct memhdr *prev;
};

struct memftr {
	size_t size;
};

static struct {
	size_t pagesize;

	struct memhdr *small;
	struct memhdr *small_top;
	struct memhdr *small_bins[SMALLBIN_COUNT];
} mstate;

static void dump(void);

static void ft_assert_impl(int pred, const char *predstr, const char *func,
			   const char *file, int line)
{
	if (!pred) {
		eprint("%s:%i: %s: Assertion '%s' failed.\n", file, line, func,
		       predstr);
		dump();
		abort();
	}
}

static struct memhdr *nexthdr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	return (struct memhdr *)((char *)chunk + hdr->size + HEADER_SIZE_INUSE);
}

static struct memftr *getftr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	ft_assert(!hdr->cinuse && "no footer present on allocated chunk");
	return (struct memftr *)((char *)chunk + hdr->size + HEADER_SIZE_INUSE -
				 FOOTER_SIZE);
}

static struct memftr *prevftr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	ft_assert(!hdr->pinuse);

	return (struct memftr *)((char *)chunk - HEADER_SIZE_INUSE);
}

static struct memhdr *prevhdr(const void *chunk)
{
	return (struct memhdr *)((char *)chunk - prevftr(chunk)->size -
				 HEADER_SIZE_INUSE);
}

static void setsize(void *chunk, size_t n)
{
	struct memhdr *hdr = (struct memhdr *)chunk;

	ft_assert(n >= MIN_ALLOC_SIZE);
	ft_assert(!hdr->cinuse);

	hdr->size = n;
	getftr(chunk)->size = n;
}

static void setinuse(void *chunk, bool val)
{
	struct memhdr *hdr = (struct memhdr *)chunk;

	hdr->cinuse = nexthdr(chunk)->pinuse = val;
}

static void *chunk2mem(const void *chunk)
{
	return (char *)chunk + HEADER_SIZE_INUSE;
}

static struct memhdr *mem2chunk(const void *p)
{
	return (struct memhdr *)((char *)p - HEADER_SIZE_INUSE);
}

static size_t small_binidx(size_t size)
{
	ft_assert(size >= 24);
	size -= 8;
	ft_assert(IS_ALGINED_TO(size, 16));
	size_t idx = size / 16;
	return idx - 1;
}

static void dump_small()
{
	const struct memhdr *cur = mstate.small;

	if (!cur) {
		eprint("nothing malloced\n");
		return;
	}

	do {
		if (cur->cinuse)
			eprint("\033[31mA");
		else
			eprint("\033[32mF");

		eprint(" %p: %p - %p: ", cur, chunk2mem(cur),
		       (char *)chunk2mem(cur) + cur->size);
		eprint(" size=%#.6zx mapped=%i pinuse=%i", cur->size,
		       cur->mapped, cur->pinuse);

		if (!cur->cinuse) {
			eprint(" next=%p prev=%p", cur->next, cur->prev);

			if (cur->size <= MAX_SMALL_SIZE)
				eprint(" bin=%zu", small_binidx(cur->size));
		}

		if (!cur->pinuse) {
			eprint(" prevhdr=%p", (void *)prevhdr(cur));
		}

		eprint("\033[m\n");

		cur = nexthdr(cur);
	} while (cur->size != 0);
}

static void dump()
{
	eprint("SMALL: %p\n", mstate.small);
	dump_small();
}

static void assert_correct_freelist(const struct memhdr *list, size_t size)
{
	const struct memhdr *cur = list;
	if (!cur)
		return;

	do {
		ft_assert(!cur->cinuse);
		ft_assert(cur->pinuse);
		ft_assert(nexthdr(cur)->cinuse);

		ft_assert(cur->size == getftr(cur)->size);
		if (size)
			ft_assert(cur->size == size);

		ft_assert(cur->next);
		ft_assert(cur->prev);

		ft_assert(cur->next->prev == cur);
		ft_assert(cur->prev->next == cur);

		cur = cur->next;
	} while (cur != list);
}

static void assert_correct_small(void)
{
	const struct memhdr *cur = mstate.small;
	if (!cur)
		return;

	const struct memhdr *prev = NULL;

	while (1) {

		ft_assert(!(!cur->cinuse && !cur->pinuse));

		if (prev)
			ft_assert(prev->cinuse == cur->pinuse);

		if (cur->size == 0)
			break;
		prev = cur;
		cur = nexthdr(cur);
	}
}

static void assert_correct(void)
{
	assert_correct_small();
	assert_correct_freelist(mstate.small_top, 0);

	size_t size = 24;
	for (int i = 0; i < SMALLBIN_COUNT; ++i) {
		assert_correct_freelist(mstate.small_bins[i], size);
		size += 16;
	}
}

static void unlink_chunk(struct memhdr **list, struct memhdr *hdr)
{
	if (hdr->next != hdr) {
		struct memhdr *next = hdr->next;
		struct memhdr *prev = hdr->prev;

		if (next == prev) {
			next->next = next->prev = next;
		} else {
			next->prev = prev;
			prev->next = next;
		}

		if (hdr == *list)
			*list = next;
	} else {
		// only element in list
		*list = NULL;
	}
	// TODO remove, not needed
	hdr->next = hdr->prev = NULL;
}

static void append_chunk(struct memhdr **list, struct memhdr *chunk)
{
	ft_assert(!chunk->cinuse);
	if (!*list) {
		*list = chunk;
		chunk->next = chunk->prev = chunk;
		return;
	}
	chunk->prev = (*list)->prev;
	chunk->next = *list;

	(*list)->prev->next = chunk;
	(*list)->prev = chunk;
}

static bool should_split(const struct memhdr *chunk, size_t allocsize)
{
	ft_assert(chunk->size >= allocsize);
	return chunk->size - allocsize >= MIN_CHUNK_SIZE;
}

static struct memhdr *split_chunk(struct memhdr *chunk, size_t n)
{
	size_t rem = chunk->size - n;

	ft_assert(rem >= MIN_CHUNK_SIZE);

	setsize(chunk, n);

	struct memhdr *next = nexthdr(chunk);
	setsize(next, rem - HEADER_SIZE_INUSE);
	setinuse(next, false);

	// TODO debug code, remove
	chunk->next = chunk->prev = next->next = next->prev = NULL;
	return next;
}

static struct memhdr **find_bin(size_t n)
{
	size_t bin = small_binidx(n);

	while (bin < SMALLBIN_COUNT) {
		struct memhdr **list = &mstate.small_bins[bin];
		if (*list)
			return list;
		bin += 1;
	}
	return NULL;
}

static void append_small_chunk(struct memhdr *chunk)
{
	ft_assert(!chunk->cinuse);

	struct memhdr **list = &mstate.small_top;
	if (chunk->size <= MAX_SMALL_SIZE) {
		size_t bin = small_binidx(chunk->size);
		list = &mstate.small_bins[bin];
	}
	append_chunk(list, chunk);
}

static struct memhdr *try_alloc_small_from_bins(size_t n)
{
	struct memhdr **bin = find_bin(n);
	if (bin) {
		struct memhdr *chunk = *bin;

		unlink_chunk(bin, chunk);
		if (should_split(chunk, n)) {
			struct memhdr *rem = split_chunk(chunk, n);
			append_small_chunk(rem);
		}
		return chunk;
	}
	return NULL;
}

static struct memhdr *find_bestfit(struct memhdr *list, size_t n)
{
	struct memhdr *cur = list;
	struct memhdr *best = NULL;

	if (!cur)
		return NULL;

	do {
		ft_assert(!cur->cinuse);
		if (cur->size == n) {
			return cur;
		} else if (cur->size > n) {
			if (!best || cur->size < best->size)
				best = cur;
		}

		ft_assert(cur->next);
		cur = cur->next;
	} while (cur != list);
	return best;
}

static struct memhdr *alloc_chunk(size_t minsize)
{
	const size_t padding = 4 * HEADER_SIZE_INUSE;

	size_t size = ROUND_UP(minsize + padding, mstate.pagesize);

	struct memhdr *chunk = mmap(NULL, size, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (chunk != MAP_FAILED) {
		// pad start so that all allocations are aligned
		chunk->size = 0;
		chunk->pinuse = true;
		setinuse(chunk, true);

		chunk = nexthdr(chunk);
		chunk->next = chunk->prev = NULL; // TODO debug code, remove
		chunk->pinuse = true;
		chunk->cinuse = false;

		size_t chunksize = (size - padding) | 8;
		setsize(chunk, chunksize);

		struct memhdr *sentinel = nexthdr(chunk);
		sentinel->size = 0;
		sentinel->pinuse = false;
		sentinel->cinuse = true;
		return chunk;
	} else {
		return NULL;
	}
}

static struct memhdr *try_alloc_small_new_chunk(size_t n)
{
	struct memhdr *chunk = find_bestfit(mstate.small_top, n);

	if (!chunk ||
	    (!should_split(chunk, n) && chunk->size > MAX_SMALL_SIZE)) {
		chunk = alloc_chunk(n);
		if (!chunk)
			return NULL;
		append_chunk(&mstate.small_top, chunk);

		if (!mstate.small)
			mstate.small = chunk;
	}

	unlink_chunk(&mstate.small_top, chunk);
	if (should_split(chunk, n)) {
		struct memhdr *rem = split_chunk(chunk, n);
		append_small_chunk(rem);
	}
	return chunk;
}

static void *malloc_small(size_t n)
{
	// memhdr addresses are guaranteed to start with 0x8 (or 0x4) so that
	// the userptr is properly aligned
	//
	// the size of the entire chunk must therefore always be a multiple of
	// 16

	// n could be already properly aligned, then we must add 0x8
	// n could be not aligned
	if (n < MIN_ALLOC_SIZE)
		n = MIN_ALLOC_SIZE;
	n = ROUND_UP(n, HALF_MALLOC_ALIGN) | HALF_MALLOC_ALIGN;
	ft_assert(n & HALF_MALLOC_ALIGN);

	struct memhdr *chunk = try_alloc_small_from_bins(n);
	if (!chunk)
		chunk = try_alloc_small_new_chunk(n);
	return chunk;
}

static void init_malloc(void)
{
	mstate.pagesize = getpagesize();

	mstate.small = NULL;
	mstate.small_top = NULL;

	for (int i = 0; i < SMALLBIN_COUNT; ++i) {
		mstate.small_bins[i] = NULL;
	}
}

void *ft_malloc(size_t n)
{
	static bool initialized = false;

	if (!initialized) {
		init_malloc();
		initialized = true;
	}

	if (!n)
		return NULL;

	// TODO lock
	assert_correct();

	struct memhdr *chunk = NULL;
	if (n <= MAX_SMALL_SIZE)
		chunk = malloc_small(n);
	else
		ft_assert(0);

	if (chunk)
		setinuse(chunk, true);

	assert_correct();
	// TODO unlock

	if (chunk) {
		void *p = chunk2mem(chunk);
		ft_assert(IS_ALGINED_TO(p, MALLOC_ALIGN));
		return p;
	}
	return NULL;
}

static void unlink_small_chunk(struct memhdr *chunk)
{
	struct memhdr **list = &mstate.small_top;

	if (chunk->size <= MAX_SMALL_SIZE) {
		size_t bin = small_binidx(chunk->size);
		list = &mstate.small_bins[bin];
	}

	unlink_chunk(list, chunk);
}

static struct memhdr *merge_chunks(struct memhdr *a, struct memhdr *b)
{
	ft_assert(a != b);
	struct memhdr *first = a < b ? a : b;

	size_t new_size = a->size + b->size + HEADER_SIZE_INUSE;

	setsize(first, new_size);
	return first;
}

static void free_small(struct memhdr *chunk)
{
	chunk->next = chunk->prev = NULL; // TODO debug code, remove

	if (!chunk->pinuse) {
		struct memhdr *prev = prevhdr(chunk);
		unlink_small_chunk(prev);
		chunk = merge_chunks(chunk, prev);
	}

	struct memhdr *next = nexthdr(chunk);
	if (!next->cinuse) {
		unlink_small_chunk(next);
		chunk = merge_chunks(chunk, next);
	}

	append_small_chunk(chunk);
}

void ft_free(void *p)
{
	if (!p)
		return;

	if (!IS_ALGINED_TO(p, MALLOC_ALIGN)) {
		eprint("free(): invalid pointer\n");
		ft_assert(0);
	}

	struct memhdr *chunk = mem2chunk(p);

	// TODO lock
	assert_correct();
	if (chunk->cinuse) {
		setinuse(chunk, false);
		if (chunk->size <= MAX_SMALL_SIZE)
			free_small(chunk);
		else
			ft_assert(0);
	} else {
		eprint("free(): pointer not allocated\n");
		ft_assert(0);
	}
	assert_correct();
}
