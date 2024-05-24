#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

#include <ft/stdio.h>
#include <unistd.h>

#define MALLOC_ALIGN (_Alignof(max_align_t))
#define HALF_MALLOC_ALIGN (MALLOC_ALIGN / 2)

#define HEADER_SIZE (sizeof(size_t))

#define MIN_CHUNK_SIZE (4 * sizeof(size_t))
#define MIN_ALLOC_SIZE (MIN_CHUNK_SIZE - HEADER_SIZE)

#define SMALLBIN_COUNT 64
#define IS_ALGINED_TO(x, boundary) (((uintptr_t)x & (boundary - 1)) == 0)

#define MAX_SMALL_SIZE 1016
#define MAX_LARGE_SIZE 1048576

#define MIN_SMALL_SIZE MIN_ALLOC_SIZE
#define MIN_LARGE_SIZE 1024

#if MAX_SMALL_SIZE >= MAX_LARGE_SIZE
#error "MAX_SMALL_SIZE must be larger than MAX_LARGE_SIZE"
#endif

_Static_assert(SMALLBIN_COUNT <= sizeof(uint64_t) * 8);
_Static_assert(2 * HEADER_SIZE == MALLOC_ALIGN);

struct memhdr {
	bool pinuse : 1;
	bool cinuse : 1;
	bool mapped : 1;
	bool small : 1;
	size_t size : (sizeof(size_t) * 8) - 4;

	// dont use when cinuse
	struct memhdr *next;
	struct memhdr *prev;
};

struct memftr {
	size_t size;
};

static struct {
	size_t pagesize;

	struct memhdr *small_top;
	struct memhdr *small_bins[SMALLBIN_COUNT];
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

static void ft_assert_impl(int pred, const char *predstr, const char *func,
			   const char *file, int line)
{
	if (!pred) {
		eprint("%s:%i: %s: Assertion '%s' failed.\n", file, line, func,
		       predstr);
		// dump();
		abort();
	}
}
#endif

static size_t small_binidx(size_t size)
{
	ft_assert(size >= MIN_ALLOC_SIZE);
	size -= HALF_MALLOC_ALIGN;
	ft_assert(IS_MULTIPLE_OF(size, MALLOC_ALIGN));
	return (size / MALLOC_ALIGN) - 1;
}

static size_t large_binidx(size_t n)
{
	n -= MIN_LARGE_SIZE;

	size_t count = 32;
	size_t size = 64;
	size_t offset = 0;
	while (count >= 2) {
		if (n <= count * size)
			return offset + n / size;

		n -= count * size;
		offset += count;
		count /= 2;
		size *= 8;
	}
	return 64 - 1;
}

static void unlink_chunk(struct memhdr **list, struct memhdr *chunk)
{
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

static void unlink_chunk_small(struct memhdr *chunk)
{
	struct memhdr **list = &mstate.small_top;

	if (chunk->size <= MAX_SMALL_SIZE) {
		size_t bin = small_binidx(chunk->size);
		list = &mstate.small_bins[bin];
	}
	unlink_chunk(list, chunk);
}

static void unlink_chunk_large(struct memhdr *chunk)
{
	struct memhdr **list = &mstate.large_top;

	if (chunk->size <= MAX_LARGE_SIZE) {
		size_t bin = large_binidx(chunk->size);
		list = &mstate.large_bins[bin];
	}
	unlink_chunk(list, chunk);
}

static struct memhdr *merge_chunks(struct memhdr *a, struct memhdr *b)
{
	ft_assert(a != b);
	struct memhdr *first = a < b ? a : b;

	size_t new_size = a->size + b->size + HEADER_SIZE;

	setsize(first, new_size);
	return first;
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

static struct memhdr *split_chunk(struct memhdr *chunk, size_t n)
{
	size_t rem = chunk->size - n;

	ft_assert(rem >= MIN_CHUNK_SIZE);

	setsize(chunk, n);

	struct memhdr *next = nexthdr(chunk);
	next->cinuse = false;
	setsize(next, rem - HEADER_SIZE);
	setinuse(next, false);

	return next;
}

static bool should_split(const struct memhdr *chunk, size_t allocsize)
{
	ft_assert(chunk->size >= allocsize);
	return chunk->size - allocsize >= MIN_CHUNK_SIZE;
}

static void maybe_initialize(void)
{
	static bool initialized = false;
	if (initialized)
		return;

	initialized = true;

	mstate.pagesize = getpagesize();
	if (!pthread_mutex_init(&mstate.mtx, NULL)) {
		eprint("this should not happen\n");
		mstate.err = true;
	}
}

static uint64_t count_trailing_zeros(uint64_t x)
{
	return __builtin_ctzll(x);
}

static struct memhdr **find_small_bin(size_t n)
{
	size_t bin = small_binidx(n);

	while (1) {
		uint64_t mask = ~((1ull << bin) - 1);
		uint64_t bins = mask & mstate.small_binmap;

		if (!bins)
			return NULL;

		uint64_t i = count_trailing_zeros(bins);

		struct memhdr **list = &mstate.small_bins[i];

		if (*list)
			return list;

		mstate.small_binmap ^= 1ull << i;
	}
}

static void append_small_chunk(struct memhdr *chunk)
{
	ft_assert(!chunk->cinuse);

	struct memhdr **list = &mstate.small_top;
	if (chunk->size <= MAX_SMALL_SIZE) {
		ft_assert(chunk->size >= MIN_SMALL_SIZE);

		size_t bin = small_binidx(chunk->size);

		mstate.small_binmap |= 1 << bin;
		list = &mstate.small_bins[bin];
	}
	append_chunk(list, chunk);
}

static void append_large_chunk(struct memhdr *chunk)
{
	ft_assert(!chunk->cinuse);

	struct memhdr **list = &mstate.large_top;
	if (chunk->size >= MIN_LARGE_SIZE && chunk->size <= MAX_LARGE_SIZE) {
		size_t bin = large_binidx(chunk->size);
		list = &mstate.large_bins[bin];
	}
	append_chunk(list, chunk);
}

static struct memhdr *malloc_small_from_bins(size_t n)
{
	ft_assert(n & HALF_MALLOC_ALIGN);

	struct memhdr **list = find_small_bin(n);
	if (*list) {
		struct memhdr *chunk = *list;

		unlink_chunk(list, chunk);
		if (should_split(chunk, n)) {
			struct memhdr *rem = split_chunk(chunk, n);
			append_small_chunk(rem);
		}
		return chunk;
	}
	return NULL;
}

static struct memhdr *malloc_large_from_bins(size_t n)
{
	ft_assert(n & HALF_MALLOC_ALIGN);

	size_t bin = large_binidx(n);
	while (bin < LARGEBIN_COUNT) {
		struct memhdr **list = &mstate.large_bins[bin];

		if (*list) {
			struct memhdr *chunk = find_bestfit(*list, n);
			ft_assert(chunk);

			unlink_chunk(list, chunk);
			if (should_split(chunk, n)) {
				struct memhdr *rem = split_chunk(chunk, n);
				append_large_chunk(rem);
			}
			return chunk;
		}
		bin += 1;
	}
}

static struct memhdr *alloc_chunk(size_t n)
{
	//TODO
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
	const size_t padding = 4 * HEADER_SIZE + MALLOC_ALIGN;

	size_t size = ROUND_UP(n + padding, mstate.pagesize);

	struct memhdr *chunk = mmap(NULL, size, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (chunk == MAP_FAILED)
		return NULL;

	chunk->size = MALLOC_ALIGN;
	chunk->pinuse = false;
	setinuse(chunk, false);
	setsize(chunk, MALLOC_ALIGN);

	chunk = nexthdr(chunk);
	chunk->pinuse = false;
	cnunk->cinuse = false;

	size_t chunksize = (size - padding) | 8;
	setsize(chunk, chunksize);

	struct memhdr *sentinel = nexthdr(chunk);
	sentinel->size = 0;
	sentinel->pinuse = false;
	sentinel->cinuse = true;
}

static size_t pad_request_size(size_t n)
{
	if (n < MIN_ALLOC_SIZE)
		n = MIN_ALLOC_SIZE;
	n = ROUND_UP(n, HALF_MALLOC_ALIGN) | HALF_MALLOC_ALIGN;
	ft_assert(n & HALF_MALLOC_ALIGN);
	return n;
}

static struct memhdr *
malloc_from_list(size_t n, struct memhdr **list_top, size_t maxsize,
		 void (*append_chunk_proc)(struct memhdr *))
{
	struct memhdr *chunk = find_bestfit(*list_top, n);

	if (!chunk || (!should_split(chunk, n) && chunk->size > maxsize))
		return NULL;

	unlink_chunk(list_top, chunk);
	if (should_split(chunk, n)) {
		struct memhdr *rem = split_chunk(chunk, n);
		append_chunk_proc(rem);
	}
	return chunk;
}

static struct memhdr *
malloc_from_new_chunk(size_t n, struct memhdr **list_top, size_t chunksize,
		      void (*append_chunk_proc)(struct memhdr *))
{
	ft_assert(chunksize >= n);
	struct memhdr *chunk = alloc_chunk(chunksize);

	if (!chunk)
		return NULL;

	if (should_split(chunk, n)) {
		struct *rem = split_chunk(chunk, n);
		append_chunk_proc(rem);
	}
	return chunk;
}


static struct memhdr *malloc_common(size_t n, size_t max_chunksize,
				    struct memhdr **list_top,
				    struct memhdr *(*malloc_from_bins)(size_t)
				    void (*append_chunk_proc))
{
	n = pad_request_size(n);

	struct memhdr *chunk =
	    malloc_from_bins(n) ||
	    malloc_from_list(n, list_top, max_chunksize, append_chunk_proc) ||
	    malloc_from_new_chunk(n, list_top, max_chunksize * 128,
				  append_chunk_proc);
	return chunk;
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

	if (n <= MAX_SMALL_SIZE) {
		chunk =
		    malloc_common(n, MAX_SMALL_SIZE, &mstate.small_top,
				  malloc_small_from_bins, append_small_chunk);
	} else if (n <= MAX_LARGE_SIZE) {
		chunk =
		    malloc_common(n, MAX_LARGE_SIZE, &mstate.large_top,
				  malloc_large_from_bins, append_large_chunk);
	} else {
		chunk = alloc_chunk(n);
	}

	if (chunk) {
		setinuse(chunk, true);
		return chunk2mem(chunk);
	}
	return NULL;
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

void *ft_malloc(size_t n)
{
	maybe_initialize();

	if (mstate.err || !n)
		return NULL;

	if (!lock())
		return NULL;

	void *p = malloc_no_lock(n);

	unlock()

	ft_assert(!p || IS_ALIGNED_TO(p, MALLOC_ALIGN));

	return p;
}

static bool is_start_chunk(const struct memhdr *chunk)
{
	return !chunk->pinuse && !chunk->cinuse;
}

static bool is_last_chunk(const struct memhdr *chunk)
{
	return chunk->size == 0;
}

static void free_common(struct memhdr *chunk, struct memhdr *list_top,
			void (*unlink_proc)(struct memhdr *),
			void (*append_chunk_proc)(struct memhdr *))
{
	setsize(chunk, chunk->size); //sets the footer

	if (!chunk->pinuse) {
		struct memhdr *prev = prevhdr(chunk);
		if (!is_start_chunk(prev)) { //likely
			unlink_proc(prev);
			chunk = merge_chunks(chunk, prev);
		}
	}

	struct memhdr *next = nexthdr(chunk);
	if (!next->cinuse) {
		unlink_proc(next);
		chunk = merge_chunks(chunk, next);
	}

	if (!chunk->pinuse) {
		//prev chunk is starting chunk
		struct memhdr *prev = prevhdr(chunk);
		next = nexthdr(chunk);

		if (is_last_chunk(next) && list_top != NULL) {
			//chunk completely free, unmap it if it's not the last
			int rc = munmap(prev, prev->size + chunk->size + 3 * HEADER_SIZE);

			if (rc) {
				perror("munmap");
				abort();
			}
			return;
		}
	}

	append_chunk_proc(chunk);
}

static void free_huge(struct memhdr *chunk)
{
	ft_assert(!chunk->pinuse);

	struct memhdr *prev = prevhdr(chunk);
	ft_assert(is_start_chunk(prev));

	if (munmap(prev, prev->size + chunk->size + 3 * HEADER_SIZE)) {
		perror("munmap");
		abort();
	}
}

static void free_no_lock(void *p)
{
	if (!p)
		return;

	if (!IS_ALIGNED_TO(p, MALLOC_ALIGN)) {
		eprint("invalid pointer\n");
		return;
	}

	struct memhdr *chunk = mem2chunk(p);

	if (!chunk->cinuse || chunk->size == 0) {
		eprint("pointer not allocated\n");
		return;
	}

	setinuse(chunk, false);

	if (chunk->size <= MAX_SMALL_SIZE) {
		free_common(chunk, mstate.small_top, unlink_chunk_small, append_small_chunk);
	} else if (chunk->size <= MAX_LARGE_SIZE) {
		free_common(chunk, mstate.large_top, unlink_chunk_large, append_large_chunk);
	} else {
		free_huge(chunk);
	}
}

void ft_free(void *p)
{
	if (!p)
		return NULL;

	if (!lock())
		return;

	free_no_lock(p);

	unlock();
}

void *ft_calloc(size_t nmemb, size_t size)
{
        size_t n = nmemb * size;

        if (nmemb && n / nmemb != size)
                return NULL;

        void *res = ft_malloc(n);
        ft_memset(res, 0, n);

        return res;
}

static struct memhdr *
realloc_common_shrink(struct memhdr *chunk, size_t newsize, size_t minsize,
		      void (*append_chunk_proc)(struct memhdr *))
{
	ft_assert(newsize < chunk->size);

	if (chunk->size - newsize < minsize) {
		//remaining chunk would be too small, just return it
		return chunk;
	}

	struct memhdr *rem = split_chunk(chunk, newsize);
	append_chunk_proc(rem);
}

static struct memhdr *realloc_common_grow_slow(struct memhdr *chunk, size_t newsize)
{
	void *newp = malloc_no_lock(newsize);
	if (!p)
		return NULL;

	void *p = chunk2mem(chunk);

	ft_memcpy(p, newp, chunk->size);

	free_no_lock(p);
	return mem2chunk(newp);
}

static struct memhdr *realloc_common_grow_fast(struct memhdr *chunk, size_t newsize)
{
	struct memhdr *next = nexthdr(chunk);
	
	ft_assert(!next->cinuse);
	ft_assert(next>size + HEADER_SIZE + chunk->size >= newsize);


	if (chunk->size - newsize >
}

static struct memhdr* realloc_common_grow(struct memhdr *chunk, size_t newsize, size_t maxsize)
{
	ft_assert(newsize >= chunk->size);

	size_t extra = newsize - chunk->size;

	struct memhdr *next = nexthdr(chunk);
	if (!next->cinuse && next->size + HEADER_SIZE >= extra) {
		size_t rem = next->size + HEADER_SIZE - extra;
		if (rem >= MIN_CHUNK_SIZE || rem < MIN_CHUNK_SIZE && newsize + rem <= maxsize) {
		}
		//we could probably just try to oversize the allocation, but
		//that just adds complexity that I don't want
	}

	//cannot resize, do it the slow way
	return realloc_common_grow_slow(chunk, newsize);
}

static int get_sizeclass(size_t n)
{
	if (n <= MAX_SMALL_SIZE)
		return 0;
	if (n <= MAX_LARGE_SIZE)
		return 1;
	return 3;
}

static struct memhdr *realloc_common(struct memhdr *chunk, size_t newsize, size_t minsize)
{
	newsize = pad_request_size(newsize);

	if (get_sizeclass(chunk->size) != get_sizeclass(newsize)) {
		//there is no efficient way to resize chunks in different zones,
		//do it the slow way
		return realloc_common_grow_slow(chunk, newsize);
	}

	if (newsize < chunk->size) {
		return realloc_common_shrink(chunk, newsize, minsize, append_chunk_proc);
	} else if (newize > chunk->size) {
		return realloc_common_grow(chunk, newsize);
	}
	return chunk; //size did not change
}

static void *realloc_no_lock(void *p, size_t newsize)
{
	ft_assert(p);
	ft_assert(newsize);

	struct memhdr *chunk = mem2chunk(p);

	if (!chunk->cinuse) {
		eprint("realloc(): invalid pointer\n");
		return NULL;
	}

	if (chunk->size <= MAX_SMALL_SIZE) {

	} else if (chunk->size <= MAX_LARGE_SIZE) {

	} else {

	}
}

void *ft_realloc(void *userptr, size_t newsize)
{
	if (!p)
		return ft_malloc(newsize);

	if (!IS_ALIGNED_TO(p, MALLOC_ALIGN)) {
		eprint("realloc(): invalid pointer\n");
		return NULL;
	}
	
	if (!size) {
		eprint("realloc(): invalid newsize\n");
		abort();
	}

	if (!lock())
		return NULL;

        void *res = ft_malloc(size);
        if (res) {
                if (userptr) {
                        struct memhdr *hdr = mem2chunk(userptr);
                        ft_memcpy(res, userptr, size > hdr->size ? hdr->size : size);
                }
                ft_free(userptr);
        }

	unlock();

        return res;
}
