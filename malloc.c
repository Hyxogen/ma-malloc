#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include <ft/stdio.h>
#include <ft/string.h>

// TODO remove
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#define FOOTER_SIZE (sizeof(size_t))
#define HEADER_SIZE (sizeof(size_t))

#define MALLOC_ALIGN (_Alignof(max_align_t))
#define HALF_MALLOC_ALIGN (MALLOC_ALIGN / 2)

#define MIN_CHUNK_SIZE (4 * sizeof(size_t))
#define MIN_ALLOC_SIZE (MIN_CHUNK_SIZE - HEADER_SIZE)

#define MIN_SMALL_SIZE MIN_ALLOC_SIZE
#define MIN_LARGE_SIZE 1024

#define MAX_SMALL_SIZE 1016
#define MAX_LARGE_SIZE 1048568

#define SMALLBIN_COUNT 64
#define LARGEBIN_COUNT 64
#define BIN_COUNT (SMALLBIN_COUNT + LARGEBIN_COUNT)
_Static_assert(SMALLBIN_COUNT <= sizeof(uint64_t) * 8, "");

#define CHUNKS_PER_ZONE 128
#define ALLOC_PADDING (2 * HEADER_SIZE + HALF_MALLOC_ALIGN)

#define ROUND_UP(x, boundary) ((x + boundary - 1) & ~(boundary - 1))
#define ROUND_DOWN(x, boundary) ((uintptr_t)x & ~(boundary - 1))
#define IS_ALIGNED_TO(x, boundary) (((uintptr_t)x & (boundary - 1)) == 0)

#define eprint(...) ft_dprintf(STDERR_FILENO, __VA_ARGS__)

#ifndef USE_FT_PREFIX
#define USE_FT_PREFIX 1
#endif

#if USE_FT_PREFIX
#else
#define ft_malloc malloc
#define ft_free free
#define ft_calloc calloc
#define ft_realloc realloc
#define ft_aligned_alloc aligned_alloc
#define ft_malloc_usable_size malloc_usable_size
#define ft_memalign memalign
#define ft_valloc valloc
#define ft_pvalloc pvalloc
#endif

#ifdef FT_NDEBUG
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

struct memhdr {
	// One should not directly modify these fields unless you know what
	// you're doing. Otherwise use the accesor functions
	bool _pinuse : 1;
	bool _small : 1;
	bool _large : 1;
	size_t _size : (sizeof(size_t) * 8) - 3;

	// DO NOT USE WHEN CHUNK IS ALLOCATED
	struct memhdr *next;
	struct memhdr *prev;
};

struct memftr {
	bool _unused : 1;
	bool _small : 1;
	bool _large : 1;
	size_t _size : (sizeof(size_t) * 8) - 3;
};

static struct {
	size_t pagesize;

	struct memhdr *debug[2];
	struct memhdr *chunk_tops[2];
	struct memhdr *bins[BIN_COUNT];
	uint64_t small_binmap;

	pthread_mutex_t mtx;

	bool err;

	int dump_fd;
} mstate;

#ifndef FT_NDEBUG
static void assert_correct(void);
#else
static void assert_correct(void) {}
#endif

static struct memftr *get_ftr_unsafe(const void *chunk);
static struct memftr *get_ftr(const void *chunk);
static bool is_inuse(const void *chunk);
static bool is_pinuse(const void *chunk);

static bool is_small(const void *chunk)
{
	const struct memhdr *hdr = (const struct memhdr *)chunk;
	return hdr->_small;
}

static bool is_large(const void *chunk)
{
	const struct memhdr *hdr = (const struct memhdr *)chunk;
	return hdr->_large;
}

static bool is_mapped(const void *chunk)
{
	return !is_small(chunk) && !is_large(chunk);
}

static size_t get_size(const void *chunk)
{
	const struct memhdr *hdr = (const struct memhdr *)chunk;

	if (is_mapped(hdr))
		return hdr->_size << 3;
	return hdr->_size;
}

static size_t get_size_ftr(const struct memftr *ftr)
{
	if (ftr->_small || ftr->_large)
		return ftr->_size;
	return ftr->_size << 3;
}

static void set_size(void *chunk, size_t n)
{
	struct memhdr *hdr = (struct memhdr *)chunk;

	ft_assert(n >= MIN_ALLOC_SIZE);

	if (is_mapped(chunk)) {
		n >>= 3;
	}

	hdr->_size = n;
	get_ftr_unsafe(chunk)->_size = n;
}

static struct memhdr *next_hdr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	return (struct memhdr *)((char *)chunk + get_size(hdr) + HEADER_SIZE);
}

static struct memftr *prev_ftr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	ft_assert(!is_pinuse(hdr));

	return (struct memftr *)((char *)chunk - HEADER_SIZE);
}

static struct memhdr *prev_hdr(const void *chunk)
{
	return (struct memhdr *)((char *)chunk - get_size_ftr(prev_ftr(chunk)) -
				 HEADER_SIZE);
}

static bool is_inuse(const void *chunk)
{
	const struct memhdr *next = next_hdr(chunk);
	return next->_pinuse;
}

static bool is_pinuse(const void *chunk)
{
	const struct memhdr *hdr = (const struct memhdr *)chunk;
	return hdr->_pinuse;
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
	ft_assert(!is_mapped(chunk));
	if (is_small(chunk))
		return 0;
	return 1;
}

static struct memftr *get_ftr_unsafe(const void *chunk)
{
	return (struct memftr *)((char *)chunk + get_size(chunk) + HEADER_SIZE -
				 FOOTER_SIZE);
}

static struct memftr *get_ftr(const void *chunk)
{
	ft_assert(!is_inuse(chunk) && "no footer present on allocated chunk");
	return get_ftr_unsafe(chunk);
}

__attribute__ ((destructor)) static void deinit(void)
{
	close(mstate.dump_fd);
}

static void maybe_initialize(void)
{
	static bool initialized = false;
	if (initialized)
		return;

	initialized = true;

	mstate.dump_fd = open("dump.txt", O_CREAT | O_TRUNC | O_WRONLY, 0777);
	if (mstate.dump_fd < 0) {
		perror("open");
		abort();
	}

	mstate.pagesize = getpagesize();
	if (pthread_mutex_init(&mstate.mtx, NULL)) {
		eprint("failed to initialize mutex");
		mstate.err = true;
	}
}

[[nodiscard]] static bool lock(void)
{
	if (pthread_mutex_lock(&mstate.mtx)) {
		// TODO set errno?
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

static bool is_sentinel(const void *chunk)
{
	const struct memhdr *hdr = (const struct memhdr *)chunk;
	return hdr->_size == 0;
}

static void set_ftr(void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	ft_assert(!is_inuse(hdr));

	struct memftr *ftr = get_ftr(hdr);
	ftr->_small = hdr->_small;
	ftr->_large = hdr->_large;
	ftr->_size = hdr->_size;
}

static void set_inuse(void *chunk, bool val)
{
	struct memhdr *next = next_hdr(chunk);

	next->_pinuse = val;

	if (!val)
		set_ftr(chunk);
}

static void *chunk2mem(const void *chunk)
{
	return (char *)chunk + HEADER_SIZE;
}

static struct memhdr *mem2chunk(const void *p)
{
	return (struct memhdr *)((char *)p - HEADER_SIZE);
}

static void append_chunk(struct memhdr **list, struct memhdr *chunk)
{
	ft_assert(!is_inuse(chunk));
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

static struct memhdr *split_chunk(struct memhdr *chunk, size_t n)
{
	ft_assert(get_size(chunk) >= n);
	size_t rem = get_size(chunk) - n;

	ft_assert(rem >= MIN_CHUNK_SIZE);
	ft_assert(!is_inuse(chunk));

	set_size(chunk, n);
	set_inuse(chunk, false);
	set_ftr(chunk);//probably not needed

	struct memhdr *next = next_hdr(chunk);

	next->_small = chunk->_small;
	next->_large = chunk->_large;
	next->_pinuse = false;
	set_size(next, rem - HEADER_SIZE);
	set_ftr(next);

	return next;
}

static struct memhdr *merge_chunks(struct memhdr *a, struct memhdr *b)
{
	ft_assert(a != b);
	ft_assert(is_small(a) == is_small(b));
	ft_assert(is_large(a) == is_large(b));
	struct memhdr *first = a < b ? a : b;

	size_t new_size = get_size(a) + get_size(b) + HEADER_SIZE;

	set_size(first, new_size);
	set_ftr(first); // TODO needed?
	return first;
}

static struct memhdr *find_bestfit(const struct memhdr *list, size_t n)
{
	const struct memhdr *cur = list;
	const struct memhdr *best = NULL;

	if (!cur)
		return NULL;

	do {
		ft_assert(!is_inuse(cur));
		if (get_size(cur) == n) {
			return (struct memhdr *)cur;
		} else if (get_size(cur) > n) {
			if (!best || get_size(cur) < get_size(best))
				best = cur;
		}

		ft_assert(cur->next);
		cur = cur->next;
	} while (cur != list);
	return (struct memhdr *)best;
}

static struct memhdr **get_list(const struct memhdr *chunk)
{
	ft_assert(!is_mapped(chunk));

	struct memhdr **list = NULL;

	if (get_size(chunk) <= MAX_LARGE_SIZE) {
		size_t bin = binidx(get_size(chunk));
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

static bool should_split(const struct memhdr *chunk, size_t allocsize)
{
	ft_assert(get_size(chunk) >= allocsize);
	return get_size(chunk) - allocsize >= MIN_CHUNK_SIZE;
}

static void maybe_split(struct memhdr *chunk, size_t n)
{
	if (should_split(chunk, n)) {
		struct memhdr *rem = split_chunk(chunk, n);
		append_chunk_any(rem);
	}
}

static struct memhdr *alloc_chunk(size_t minsize, bool small, bool large)
{
	ft_assert(!(small && large));

	const size_t padding = 2 * HEADER_SIZE + HALF_MALLOC_ALIGN;

	size_t mmap_size = ROUND_UP(minsize + ALLOC_PADDING, mstate.pagesize);
	struct memhdr *chunk = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (chunk == MAP_FAILED)
		return NULL;

	_Static_assert(2 * HEADER_SIZE == MALLOC_ALIGN,
		       "basic assumption for alloc_chunk");
	// make sure that all header addresses start with 0x8
	chunk = (struct memhdr *)((uintptr_t)chunk | HALF_MALLOC_ALIGN);

	chunk->_small = small;
	chunk->_large = large;
	chunk->_pinuse = true;

	size_t chunksize = mmap_size - padding;
	set_size(chunk, chunksize);

	set_ftr(chunk);

	struct memhdr *sentinel = next_hdr(chunk);
	sentinel->_pinuse = false;
	sentinel->_small = small;
	sentinel->_large = large;
	sentinel->_size = 0;

	return chunk;
}

static uint64_t count_trailing_zeros(uint64_t x) { return __builtin_ctzll(x); }

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
	bool small = freelist_idx_from_size(n) == 0;
	size_t chunksize = small ? MAX_SMALL_SIZE : MAX_LARGE_SIZE;
	chunksize *= CHUNKS_PER_ZONE;

	struct memhdr *chunk = alloc_chunk(chunksize, small, !small);
	ft_assert(is_small(chunk) || is_large(chunk));

	size_t idx = freelist_idx(chunk);
	if (!mstate.debug[idx])
		mstate.debug[idx] = chunk;
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

static bool should_unmap(const struct memhdr *chunk)
{
	if (is_mapped(chunk))
		return true;
	if (!is_sentinel(next_hdr(chunk)))
		return false;

	size_t threshold = is_small(chunk) ? MAX_SMALL_SIZE : MAX_LARGE_SIZE;
	threshold *= CHUNKS_PER_ZONE;
	threshold += ALLOC_PADDING;
	threshold = ROUND_UP(threshold, mstate.pagesize);
	threshold -= ALLOC_PADDING;

	size_t idx = freelist_idx(chunk);

	if (!mstate.chunk_tops[idx])
		return false;
	return get_size(chunk) >= threshold;
}

static void dealloc_chunk(struct memhdr *chunk)
{
	void *start = (void *)ROUND_DOWN(chunk, mstate.pagesize);

	if (munmap(start, get_size(chunk) + ALLOC_PADDING)) {
		perror("munmap");
		abort();
	}
}

static void free_common(struct memhdr *chunk)
{
	if (!is_pinuse(chunk)) {
		struct memhdr *prev = prev_hdr(chunk);
		unlink_chunk_any(prev);
		chunk = merge_chunks(chunk, prev);
	}

	struct memhdr *next = next_hdr(chunk);
	if (!is_sentinel(next) && !is_inuse(next)) {
		unlink_chunk_any(next);
		chunk = merge_chunks(chunk, next);
	}

	if (should_unmap(chunk)) {
		dealloc_chunk(chunk);
	} else {
		append_chunk_any(chunk);
	}
}

static bool check_requestsize(size_t n)
{
	if (mstate.err | !n)
		return false;

	if (n > PTRDIFF_MAX) {
		errno = ENOMEM;
		return false;
	}
	return true;
}

static bool check_chunk(const struct memhdr *chunk)
{
	if (get_size(chunk) == 0)
		return false;
	if (is_large(chunk) && is_small(chunk))
		return false;
	if (is_large(chunk) || is_small(chunk))
		return get_size(chunk) <= MAX_LARGE_SIZE + MIN_CHUNK_SIZE;
	return get_size(chunk) > MAX_LARGE_SIZE;
}

static void *malloc_no_lock(size_t n)
{
	if (!check_requestsize(n))
		return NULL;

	struct memhdr *chunk = NULL;

	if (n <= MAX_LARGE_SIZE)
		chunk = malloc_common(n);
	else
		chunk = alloc_chunk(n, false, false);

	if (chunk) {
		set_inuse(chunk, true);
		return chunk2mem(chunk);
	}
	return NULL;
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

	if (!is_inuse(chunk) || !check_chunk(chunk)) {
		eprint("free(): invalid pointer\n");
		abort();
	}

	set_inuse(chunk, false);

	free_common(chunk);
}

void *ft_malloc(size_t n)
{
	maybe_initialize();

#if TRACES
	ft_dprintf(mstate.dump_fd, "//ft_malloc(%zu);\n", n);
#endif

	if (!check_requestsize(n))
		return NULL; // fast path before lock

	if (!lock())
		return NULL;

	assert_correct();
	void *p = malloc_no_lock(n);
	assert_correct();

	unlock();

	ft_assert(!p || IS_ALIGNED_TO(p, MALLOC_ALIGN));

#if TRACES
	ft_dprintf(mstate.dump_fd, "void *tmp_%p = ft_malloc(%zu);\n", p, n);
#endif

	return p;
}

void ft_free(void *p)
{
	if (!p) {
#if TRACES
		ft_dprintf(mstate.dump_fd, "ft_free(NULL);\n");
#endif
		return;
	}
#if TRACES
	ft_dprintf(mstate.dump_fd, "ft_free(tmp_%p);\n", p);
#endif

	if (!lock())
		return;

	assert_correct();
	free_no_lock(p);
	assert_correct();

	unlock();
}

void *ft_calloc(size_t nmemb, size_t size)
{
#if TRACES
	ft_dprintf(mstate.dump_fd, "//ft_calloc(%zu, %zu);\n", nmemb, size);
#endif
        size_t n = nmemb * size;

        if (nmemb && n / nmemb != size)
                return NULL;

        void *res = ft_malloc(n);
	//TODO huge allocations don't have to be zeroed
        ft_memset(res, 0, n);

        return res;
}

void *ft_realloc(void *userptr, size_t size)
{
	//TODO add sanity checks
        void *res = ft_malloc(size);
        if (res) {
                if (userptr) {
                        struct memhdr *hdr = mem2chunk(userptr);
                        ft_memcpy(res, userptr, size > get_size(hdr) ? get_size(hdr) : size);
                }
                ft_free(userptr);
        }

        return res;
}

static void *aligned_alloc_no_lock(size_t align, size_t size)
{
	ft_assert(align > MALLOC_ALIGN);

	if (!check_requestsize(size))
		return NULL;

	void *p = malloc_no_lock(size + 2 * align - 1);
	if (!p)
		return NULL;

	void *alignedp = (void*) ROUND_UP((uintptr_t) p, align);

	if (p != alignedp) {
		size_t diff = alignedp - p;

		if (diff < MIN_CHUNK_SIZE) {
			alignedp = (void*) ROUND_UP((uintptr_t) alignedp + 1, align);
			diff = alignedp - p;
		}

		ft_assert(diff >= MIN_CHUNK_SIZE);

		struct memhdr *chunk = mem2chunk(p);

		set_inuse(chunk, false);
		struct memhdr *aligned_chunk = split_chunk(chunk, diff - HEADER_SIZE);
		set_inuse(aligned_chunk, true);

		ft_assert(get_size(aligned_chunk) >= size);
		ft_assert(chunk2mem(aligned_chunk) == alignedp);
		
		if (!is_mapped(chunk))
			append_chunk_any(chunk);
	}

	return alignedp;
}

void *ft_aligned_alloc(size_t align, size_t size)
{
	if (align <= MALLOC_ALIGN)
		return ft_malloc(size);

	maybe_initialize();

	if (!check_requestsize(size))
		return NULL;

	if (!lock())
		return NULL;

	void *p = aligned_alloc_no_lock(align, size);

	unlock();

	ft_assert(!p || IS_ALIGNED_TO(p, align));
	return p;
}

size_t ft_malloc_usable_size(void *userptr)
{
        if (!userptr)
                return 0;
	//TODO add sanity checks
        return get_size(mem2chunk(userptr));
}

void *ft_memalign(size_t align, size_t size)
{
        (void)align;
        (void)size;
	errno = ENOMEM;
	ft_assert(0);
        return NULL;
}

void *ft_valloc(size_t size)
{
        (void)size;
	errno = ENOMEM;
	ft_assert(0);
        return NULL;
}

void *ft_pvalloc(size_t size)
{
        (void) size;
	errno = ENOMEM;
	ft_assert(0);
        return NULL;
}

static void dump_chunk(const struct memhdr *chunk)
{
	if (is_sentinel(chunk)) {
		eprint("%p SENTINEL\n", chunk);
		return;
	}

	bool inuse = is_inuse(chunk);

	if (inuse)
		eprint("\033[31m");
	else
		eprint("\033[32m");

	size_t size = get_size(chunk);

	eprint("%p: %p - %p: ", chunk, chunk2mem(chunk),
	       (char *)chunk2mem(chunk) + size);
	eprint("p %i s %i l %i _size=%7zu size=%7zu", is_pinuse(chunk),
	       is_small(chunk), is_large(chunk), chunk->_size, size);

	if (!inuse && !is_mapped(chunk)) {
		if (size <= MAX_LARGE_SIZE) {
			eprint(" bin=%3zu", binidx(size));
		}

		eprint(" next=%p prev=%p", chunk->next, chunk->prev);
	}
	eprint("\033[m\n");
}

static void dump_list(const struct memhdr *list)
{
	const struct memhdr *cur = list;
	if (!cur)
		return;

	while (1) {
		dump_chunk(cur);

		if (is_sentinel(cur))
			break;
		cur = next_hdr(cur);
	}
}

static void dump(void)
{
	eprint("SMALL:\n");
	dump_list(mstate.debug[0]);
	eprint("LARGE:\n");
	dump_list(mstate.debug[1]);
}

#ifndef FT_NDEBUG
static void assert_correct_chunk(const struct memhdr *chunk)
{
	if (is_sentinel(chunk))
		return;

	ft_assert(!(is_small(chunk) && is_large(chunk)));

	if (is_mapped(chunk))
		ft_assert(get_size(chunk) > MAX_LARGE_SIZE);
	else if (is_inuse(chunk))
		ft_assert(get_size(chunk) < MAX_LARGE_SIZE + MIN_CHUNK_SIZE);

	struct memhdr *next = next_hdr(chunk);

	if (is_inuse(chunk)) {
		/*if (is_small(chunk))
			ft_assert(get_size(chunk) < MAX_SMALL_SIZE +
		MIN_CHUNK_SIZE); if (is_large(chunk)) ft_assert(get_size(chunk)
		< MAX_LARGE_SIZE + MIN_CHUNK_SIZE);*/

		ft_assert(is_pinuse(next) == true);
	} else {
		struct memftr *ftr = get_ftr(chunk);

		ft_assert(chunk->_small == ftr->_small);
		ft_assert(chunk->_large == ftr->_large);
		ft_assert(chunk->_size == ftr->_size);

		ft_assert(chunk->next);
		ft_assert(chunk->prev);

		ft_assert(chunk->next->prev == chunk);
		ft_assert(chunk->prev->next == chunk);

		ft_assert(is_pinuse(chunk) && "chunks should be merged");

		ft_assert(is_pinuse(next) == false);

		if (!is_sentinel(next))
			ft_assert(is_inuse(next) && "chunks should be merged");
	}
}

static void assert_correct_list(const struct memhdr *list)
{
	const struct memhdr *cur = list;
	if (!cur)
		return;

	while (!is_sentinel(cur)) {
		assert_correct_chunk(cur);
		cur = next_hdr(cur);
	}
}

static void assert_correct_bin(const struct memhdr *list, size_t min,
			       size_t max)
{
	const struct memhdr *cur = list;
	if (!cur)
		return;

	do {
		ft_assert(!is_inuse(cur));
		size_t size = get_size(cur);
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

	size_t size = 24;
	for (int i = 0; i < SMALLBIN_COUNT; ++i) {
		assert_correct_bin(mstate.bins[i], size, size);
		size += 16;
	}
}
#endif
