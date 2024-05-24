#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

#include <unistd.h>

#include <sys/mman.h>

#include <ft/stdio.h>

#define MALLOC_ALIGN (_Alignof(max_align_t))
#define HALF_MALLOC_ALIGN (MALLOC_ALIGN / 2)

#define MIN_CHUNK_SIZE (4 * sizeof(size_t))
#define MIN_ALLOC_SIZE (MIN_CHUNK_SIZE - HEADER_SIZE)

#define MAX_SMALL_SIZE 1016
#define MAX_LARGE_SIZE 1048576

#define MIN_SMALL_SIZE MIN_ALLOC_SIZE
#define MIN_LARGE_SIZE 1024

#if MAX_SMALL_SIZE >= MAX_LARGE_SIZE
#error "MAX_SMALL_SIZE must be larger than MAX_LARGE_SIZE"
#endif

#define SMALLBIN_COUNT 64
#define LARGEBIN_COUNT 64
#define BIN_COUNT (SMALLBIN_COUNT + LARGEBIN_COUNT)

#define FOOTER_SIZE (sizeof(size_t))
#define HEADER_SIZE (sizeof(size_t))

#define ROUND_UP(x, boundary) ((x + boundary - 1) & ~(boundary - 1))
#define ROUND_DOWN(x, boundary) ((uintptr_t) x & ~(boundary - 1))
#define IS_ALIGNED_TO(x, boundary) (((uintptr_t)x & (boundary - 1)) == 0)

#define CHUNKS_PER_ZONE 128
#define ALLOC_PADDING (2 * HEADER_SIZE + HALF_MALLOC_ALIGN)

_Static_assert(SMALLBIN_COUNT <= sizeof(uint64_t) * 8, "");

struct memhdr {
	bool pinuse : 1;
	bool cinuse : 1;
	bool mapped : 1;
	bool small : 1;
	size_t _size : (sizeof(size_t) * 8) - 4;

	// DO NOT USE WHEN cinuse
	struct memhdr *next;
	struct memhdr *prev;
};

struct memftr {
	bool pinuse : 1;
	bool cinuse : 1;
	bool mapped : 1;
	bool small : 1;
	size_t _size : (sizeof(size_t) * 8) - 4;
};

static struct {
	size_t pagesize;

	struct memhdr *debug[2];
	struct memhdr *chunk_tops[2];
	struct memhdr *bins[BIN_COUNT];
	uint64_t small_binmap;

	pthread_mutex_t mtx;
	
	bool err;
} mstate;

#define eprint(...) ft_dprintf(STDERR_FILENO, __VA_ARGS__)

#ifdef NDEBUG
#define ft_assert(pred)
#else
#include <stdlib.h>
#define ft_assert(pred)                                                        \
	ft_assert_impl(!(!(pred)), #pred, __FUNCTION__, __FILE__, __LINE__)
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
#endif

static void assert_correct(void);

static size_t getsize_ftr(const struct memftr *ftr)
{
	if (ftr->mapped)
		return ftr->_size << 4;
	return ftr->_size;
}

static size_t getsize(const void *chunk)
{
	const struct memhdr *hdr = (const struct memhdr*)chunk;

	if (hdr->mapped)
		return hdr->_size << 4;
	return hdr->_size;
}

static struct memhdr *nexthdr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	return (struct memhdr *)((char *)chunk + getsize(hdr) + HEADER_SIZE);
}

static struct memftr *prevftr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	ft_assert(!hdr->pinuse);

	return (struct memftr *)((char *)chunk - HEADER_SIZE);
}

static struct memftr *getftr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	ft_assert(!hdr->cinuse && "no footer present on allocated chunk");
	return (struct memftr *)((char *)chunk + getsize(hdr) + HEADER_SIZE -
				 FOOTER_SIZE);
}

static struct memhdr *prevhdr(const void *chunk)
{
	return (struct memhdr *)((char *)chunk - getsize_ftr(prevftr(chunk)) -
				 HEADER_SIZE);
}

static void setftr(void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	ft_assert(!hdr->cinuse);

	struct memftr *ftr = getftr(hdr);
	ftr->pinuse = hdr->pinuse;
	ftr->cinuse = hdr->cinuse;
	ftr->mapped = hdr->mapped;
	ftr->small = hdr->small;
	ftr->_size = hdr->_size;
}

static void setsize(void *chunk, size_t n)
{
	struct memhdr *hdr = (struct memhdr *)chunk;

	ft_assert(n >= MIN_ALLOC_SIZE);
	ft_assert(!hdr->cinuse);

	if (hdr->mapped) {
		n >>= 4;
	}

	hdr->_size = n;
	getftr(chunk)->_size = n;
}

static void setinuse(void *chunk, bool val)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	struct memhdr *next = nexthdr(chunk);

	hdr->cinuse = next->pinuse = val;
	
	if (!val)
		getftr(hdr)->cinuse = true;
	if (!next->cinuse)
		getftr(next)->pinuse = true;
}

static size_t small_binidx(size_t size)
{
	ft_assert(size >= MIN_ALLOC_SIZE);
	size -= HALF_MALLOC_ALIGN;
	ft_assert(IS_ALIGNED_TO(size, MALLOC_ALIGN));
	return (size / MALLOC_ALIGN) - 1;
}

static size_t large_binidx(size_t n)
{
	n -= MIN_LARGE_SIZE;

	size_t count = 32;
	size_t size = 64;
	size_t offset = SMALLBIN_COUNT;
	while (count >= 2) {
		if (n <= count * size)
			return offset + n / size;

		n -= count * size;
		offset += count;
		count /= 2;
		size *= 8;
	}
	return SMALLBIN_COUNT + 64 - 1;
}

static size_t binidx(size_t n)
{
	if (n <= MAX_SMALL_SIZE)
		return small_binidx(n);
	else if (n <= MAX_LARGE_SIZE)
		return large_binidx(n);
	else
		ft_assert(0 && "invalid size");
	return -1;
}

static size_t freelist_idx_from_size(size_t n)
{
	if (n <= MAX_SMALL_SIZE)
		return 0;
	ft_assert(n <= MAX_LARGE_SIZE);
	return 1;
}

static size_t freelist_idx(const struct memhdr *chunk)
{
	ft_assert(!chunk->mapped);
	if (chunk->small)
		return 0;
	return 1;
}

static void *chunk2mem(const void *chunk)
{
	return (char *)chunk + HEADER_SIZE;
}

static struct memhdr *mem2chunk(const void *p)
{
	return (struct memhdr *)((char *)p - HEADER_SIZE);
}


static struct memhdr *split_chunk(struct memhdr *chunk, size_t n)
{
	size_t rem = getsize(chunk) - n;

	ft_assert(rem >= MIN_CHUNK_SIZE);

	setsize(chunk, n);
	setftr(chunk);

	struct memhdr *next = nexthdr(chunk);
	next->cinuse = false;
	next->mapped = chunk->mapped;
	next->small = chunk->small;
	setsize(next, rem - HEADER_SIZE);
	setinuse(next, false);
	setftr(next);

	return next;
}

static void unlink_chunk(struct memhdr **list, struct memhdr *chunk)
{
	ft_assert(*list);

	if (chunk->next != chunk) {
		struct memhdr *next = chunk->next;
		struct memhdr *prev = chunk->prev;

		if (next == prev) {
			next->next = next->prev = next;
		} else {
			next->prev = prev;
			prev->next = next;
		}

		if (chunk == *list)
			*list = next;
	} else {
		// only element in list
		*list = NULL;
	}
}

static struct memhdr *find_bestfit(const struct memhdr *list, size_t n)
{
	const struct memhdr *cur = list;
	const struct memhdr *best = NULL;

	if (!cur)
		return NULL;

	do {
		ft_assert(!cur->cinuse);
		if (getsize(cur) == n) {
			return (struct memhdr*) cur;
		} else if (getsize(cur) > n) {
			if (!best || getsize(cur) < getsize(best))
				best = cur;
		}

		ft_assert(cur->next);
		cur = cur->next;
	} while (cur != list);
	return (struct memhdr*) best;
}

static bool should_split(const struct memhdr *chunk, size_t allocsize)
{
	ft_assert(getsize(chunk) >= allocsize);
	return getsize(chunk) - allocsize >= MIN_CHUNK_SIZE;
}

static void maybe_initialize(void)
{
	static bool initialized = false;
	if (initialized)
		return;

	initialized = true;

	mstate.pagesize = getpagesize();
	if (pthread_mutex_init(&mstate.mtx, NULL)) {
		eprint("failed to initialize mutex");
		mstate.err = true;
	}
}

[[nodiscard]] static bool lock(void)
{
	if (pthread_mutex_lock(&mstate.mtx)) {
		eprint("failed to get lock on mutex");
		return false;
	}
	return true;
}

static void unlock(void)
{
	if (pthread_mutex_unlock(&mstate.mtx)) {
		eprint("failed to unlock mutex");
		abort();
	}
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

static struct memhdr *merge_chunks(struct memhdr *a, struct memhdr *b)
{
	ft_assert(a != b);
	ft_assert(a->mapped == b->mapped);
	ft_assert(a->small == b->small);
	struct memhdr *first = a < b ? a : b;

	size_t new_size = getsize(a) + getsize(b) + HEADER_SIZE;

	setsize(first, new_size);
	setftr(first);
	return first;
}

static uint64_t count_trailing_zeros(uint64_t x)
{
	return __builtin_ctzll(x);
}

static struct memhdr *find_small_bin(size_t n, struct memhdr ***from)
{
	size_t bin = small_binidx(n);

	while (1) {
		uint64_t mask = ~((1ull << bin) - 1);
		uint64_t bins = mask & mstate.small_binmap;

		if (!bins)
			return NULL;

		uint64_t i = count_trailing_zeros(bins);

		struct memhdr **list = &mstate.bins[i];

		if (*list) {
			*from = list;
			return *list;
		}

		mstate.small_binmap ^= 1ull << i;
	}
}

static struct memhdr *find_large_bin(size_t n, struct memhdr ***from)
{
	size_t bin = large_binidx(n);

	while (bin < BIN_COUNT) {
		struct memhdr **list = &mstate.bins[bin];

		if (*list) {
			struct memhdr *chunk = find_bestfit(*list, n);
			if (chunk) {
				*from = list;
				return chunk;
			}
		}
		bin += 1;
	}
	return NULL;
}

static struct memhdr *find_bin(size_t n, struct memhdr ***from)
{
	if (n <= MAX_SMALL_SIZE)
		return find_small_bin(n, from);
	else if (n <= MAX_LARGE_SIZE)
		return find_large_bin(n, from);
	else
		ft_assert(0 && "invalid size");
	return NULL;
}

static struct memhdr **get_list(const struct memhdr *chunk)
{
	ft_assert(!chunk->mapped);

	struct memhdr **list = NULL;

	if (getsize(chunk) <= MAX_LARGE_SIZE) {
		size_t bin = binidx(getsize(chunk));
		list = &mstate.bins[bin];
	} else {
		size_t idx = freelist_idx(chunk);
		list = &mstate.chunk_tops[idx];
	}

	return list;
}

static void append_chunk_any(struct memhdr *chunk)
{
	struct memhdr **list = get_list(chunk);
	append_chunk(list, chunk);
}

static void unlink_chunk_any(struct memhdr *chunk)
{
	struct memhdr **list = get_list(chunk);
	unlink_chunk(list, chunk);
}

static void maybe_split(struct memhdr *chunk, size_t n)
{
	if (should_split(chunk, n)) {
		struct memhdr *rem = split_chunk(chunk, n);
		append_chunk_any(rem);
	}
}

static struct memhdr *alloc_chunk(size_t n, bool mapped, bool small)
{
	//you must be able to determine if a chunk is the first chunk. We could
	//do this in two ways:
	//
	//make size 0
	//set cinuse and pinuse as false
	//
	//size 0 would not work, as it does not allow for a footer, which it
	//must have if we want to dermine if the previous chunk of a
	//non-terminal chunk is the start
	//
	//thus we must use the cinuse and pinuse method
	//
	//we must have a footer thus the size of the chunk must be at least
	//8
	//
	//we must make sure that the next chunk header start with 0x8,
	//thus the size of the chunk must be 16
	//
	//this means that the first non-terminal starts at 0x18
	ft_assert(!(mapped && small));

	const size_t padding = 3 * HEADER_SIZE;

	size_t size = ROUND_UP(n + padding, mstate.pagesize);
	struct memhdr *start = mmap(NULL, size, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (start == MAP_FAILED)
		return NULL;

	_Static_assert(2 * HEADER_SIZE == MALLOC_ALIGN, "basic assumption for alloc_chunk");

	start->mapped = mapped;
	start->small = small;
	start->cinuse = start->pinuse = false;
	start->_size = 0;

	struct memhdr *chunk = nexthdr(start);
	chunk->pinuse = false;
	chunk->cinuse = false;
	chunk->mapped = mapped;
	chunk->small = small;
	setftr(chunk);

	size_t chunksize = size - padding;
	setsize(chunk, chunksize);

	struct memhdr *sentinel = nexthdr(chunk);
	sentinel->pinuse = false;
	sentinel->cinuse = true;
	sentinel->_size = 0;
	sentinel->mapped = mapped;
	sentinel->small = small;

	return chunk;
}

static struct memhdr *malloc_from_bins(size_t n)
{
	struct memhdr **list = NULL;
	struct memhdr *chunk = find_bin(n, &list);

	if (!chunk)
		return NULL;
	ft_assert(list);

	unlink_chunk(list, chunk);
	maybe_split(chunk, n);
	return chunk;
}

static struct memhdr *malloc_from_freelists(size_t n)
{
	size_t idx = freelist_idx_from_size(n);
	struct memhdr **list = &mstate.chunk_tops[idx];

	struct memhdr *chunk = find_bestfit(*list, n);
	if (chunk) {
		unlink_chunk(list, chunk);
		maybe_split(chunk, n);
	}
	return chunk;
}

static struct memhdr *malloc_from_new_chunk(size_t n)
{
	size_t chunksize = freelist_idx_from_size(n) == 0 ? MAX_SMALL_SIZE : MAX_LARGE_SIZE;
	chunksize *= 128;

	struct memhdr *chunk = alloc_chunk(chunksize, false, n <= MAX_SMALL_SIZE);

	mstate.debug[freelist_idx(chunk)] = chunk;
	if (chunk)
		maybe_split(chunk, n);
	return chunk;
}

static size_t pad_request_size(size_t n)
{
	if (n < MIN_ALLOC_SIZE)
		n = MIN_ALLOC_SIZE;
	n = ROUND_UP(n, HALF_MALLOC_ALIGN) | HALF_MALLOC_ALIGN;
	ft_assert(n & HALF_MALLOC_ALIGN);
	return n;
}

static struct memhdr *malloc_common(size_t n)
{
	n = pad_request_size(n);

	struct memhdr *chunk = malloc_from_bins(n);
	if (!chunk) {
		chunk = malloc_from_freelists(n);
		if (!chunk)
			chunk = malloc_from_new_chunk(n);
	}

	return chunk;
}

static bool is_sentinel(size_t size)
{
	return size == 0;
}


static bool is_first_chunk(const struct memhdr *chunk)
{
	if (chunk->pinuse)
		return false;
	struct memftr *prev = prevftr(chunk);
	return is_sentinel(prev->_size);
}

static bool is_end_chunk(const struct memhdr *chunk)
{
	struct memhdr *next = nexthdr(chunk);
	return is_sentinel(next->_size);
}

static bool should_unmap(const struct memhdr *chunk) 
{
	if (!is_first_chunk(chunk) || !is_end_chunk(chunk))
		return false;

	size_t idx = freelist_idx(chunk);

	if (!mstate.chunk_tops[idx])
		return false;
	return true;
}

static void dealloc_chunk(struct memhdr *chunk)
{
	ft_assert(is_first_chunk(chunk));
	ft_assert(is_end_chunk(chunk));

	struct memftr *prev = prevftr(chunk);

	if (munmap(prev, getsize(chunk) + 3 * HEADER_SIZE)) {
		perror("munmap");
		abort();
	}
}

static void free_common(struct memhdr *chunk)
{
	if (!chunk->pinuse && !is_first_chunk(chunk)) {
		struct memhdr *prev = prevhdr(chunk);
		unlink_chunk_any(prev);
		chunk = merge_chunks(chunk, prev);
	}

	struct memhdr *next = nexthdr(chunk);
	if (!next->cinuse) {
		unlink_chunk_any(next);
		chunk = merge_chunks(chunk, next);
	}

	if (should_unmap(chunk)) {
		dealloc_chunk(chunk);
	} else {
		append_chunk_any(chunk);
	}
}

static void free_huge(struct memhdr *chunk)
{
	ft_assert(chunk->mapped);
	dealloc_chunk(chunk);
}

static void *malloc_no_lock(size_t n)
{
	if (!n)
		return NULL;

	if (n > PTRDIFF_MAX) {
		errno = ENOMEM;
		return NULL;
	}

	struct memhdr *chunk = NULL;

	if (n <= MAX_LARGE_SIZE)
		chunk = malloc_common(n);
	else
		chunk = alloc_chunk(n, true, false);

	if (chunk) {
		setinuse(chunk, true);
		return chunk2mem(chunk);
	}
	return NULL;
}

static bool is_valid_chunk(const struct memhdr *chunk)
{
	if (getsize(chunk) == 0)
		return false;
	if (chunk->mapped && chunk->small)
		return false;
	if (chunk->mapped)
		return getsize(chunk) > MAX_LARGE_SIZE;
	return true;
}

static void free_no_lock(void *p)
{
	if (!p)
		return;

	if (!IS_ALIGNED_TO(p, MALLOC_ALIGN)) {
		eprint("free(): invalid pointer\n");
		abort();
	}

	struct memhdr *chunk = mem2chunk(p);

	if (!chunk->cinuse || !is_valid_chunk(chunk)) {
		eprint("free(): pointer not allocated\n");
		abort();
	}

	setinuse(chunk, false);
	setftr(chunk);

	if (chunk->mapped) {
		free_huge(chunk);
	} else {
		free_common(chunk);
	}
}

void *ft_malloc(size_t n)
{
	maybe_initialize();

	if (mstate.err | !n)
		return NULL;

	if (!lock())
		return NULL;

	assert_correct();
	void *p = malloc_no_lock(n);
	assert_correct();

	unlock();

	ft_assert(!p || IS_ALIGNED_TO(p, MALLOC_ALIGN));
	
	return p;
}

void ft_free(void *p)
{
	if (!p)
		return;

	if (!lock())
		return;

	assert_correct();
	free_no_lock(p);
	assert_correct();

	unlock();
}

static void dump_chunk(const struct memhdr *chunk)
{
	if (is_sentinel(chunk->_size)) {
		eprint("%p: SENTINEL\n", chunk);
		return;
	}

	if (chunk->cinuse)
		eprint("\033[31m");
	else
		eprint("\033[32m");

	size_t size = getsize(chunk);

	eprint("%p: %p - %p: ", chunk, chunk2mem(chunk), (char*) chunk2mem(chunk) + getsize(chunk));
	eprint("pinuse=%i cinuse=%i mapped=%i small=%i _size=%4zu size=%7zu",
	       chunk->pinuse, chunk->cinuse, chunk->mapped, chunk->small,
	       chunk->_size, size);

	if (!chunk->cinuse && !chunk->mapped) {
		if (size <= MAX_LARGE_SIZE) {
			eprint(" bin=%3zu", binidx(size));
		}

		eprint(" next=%p prev=%p", chunk->next, chunk->prev);
	}


	eprint("\033[m\n");
}

static void dump_list(const struct memhdr *hdr)
{
	const struct memhdr *cur = hdr;

	if (!cur)
		return;

	while (1) {
		dump_chunk(cur);
		
		if (is_sentinel(cur->_size))
			break;
		cur = nexthdr(cur);
	}
}

static void dump(void)
{
	eprint("SMALL:\n");
	dump_list(mstate.debug[0]);
	eprint("LARGE:\n");
	dump_list(mstate.debug[1]);
}

static void assert_correct_chunk(const struct memhdr *chunk)
{
	if (!chunk->pinuse && !chunk->cinuse) {
		ft_assert(is_first_chunk(chunk) && "chunks should be merged");
	}

	ft_assert(!(chunk->mapped && chunk->small));

	if (!chunk->cinuse) {
		ft_assert(nexthdr(chunk)->cinuse && "chunks should be merged");

		ft_assert(chunk->next);
		ft_assert(chunk->prev);

		ft_assert(chunk->next->prev == chunk);
		ft_assert(chunk->prev->next == chunk);

		struct memftr *ftr = getftr(chunk);
		ft_assert(ftr->pinuse == chunk->pinuse);
		ft_assert(ftr->cinuse == chunk->cinuse);
		ft_assert(ftr->mapped == chunk->mapped);
		ft_assert(ftr->small == chunk->small);
		ft_assert(ftr->_size == chunk->_size);
	}
}

static void assert_correct_list(const struct memhdr *list)
{
	const struct memhdr *cur = list;

	if (!cur)
		return;

	while (!is_sentinel(cur->_size)) {
		assert_correct_chunk(cur);
		cur = nexthdr(cur);
	}
}

static void assert_correct_freelist(const struct memhdr *list, size_t min, size_t max)
{
	const struct memhdr *cur = list;
	if (!cur)
		return;

	do {
		ft_assert(!cur->cinuse);

		if (!cur->pinuse) {
			ft_assert(is_first_chunk(cur));
		}
		ft_assert(nexthdr(cur)->cinuse);

		ft_assert(cur->_size == getftr(cur)->_size);

		size_t size = getsize(cur);
		ft_assert(size >= min);
		ft_assert(size <= max);

		ft_assert(cur->next);
		ft_assert(cur->prev);

		ft_assert(cur->next->prev == cur);
		ft_assert(cur->prev->next == cur);

		cur = cur->next;
	} while (cur != list);
}

static void assert_correct(void)
{
	assert_correct_list(mstate.debug[0]);
	assert_correct_list(mstate.debug[1]);

	assert_correct_freelist(mstate.chunk_tops[0], 0, -1);
	assert_correct_freelist(mstate.chunk_tops[1], 0, -1);

	size_t size = 24;
	for (int i = 0; i < SMALLBIN_COUNT; ++i) {
		assert_correct_freelist(mstate.bins[i], size, size);
		size += 16;
	}
}
