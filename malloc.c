#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

#include <ft/stdio.h>
#include <ft/string.h>

#include <sys/mman.h>

#define MALLOC_ALIGN (_Alignof(max_align_t))
#define ROUND_UP(x, boundary) ((x + boundary - 1) & ~(boundary - 1))
#define IS_ALGINED_TO(x, boundary) (((uintmax_t) x & (boundary - 1)) == 0)
#define IS_MULTIPLE_OF(x, y) (((x / y) * y) == x)

#define MIN_CHUNK_SIZE 32
#define MIN_ALLOC_SIZE (MIN_CHUNK_SIZE - sizeof(struct memhdr))
#define SMALL_COUNT 128
#define MAX_SMALL_SIZE 1016
#define MAX_LARGE_SIZE 1048576

#define SMALLBIN_COUNT 64

#ifdef NDEBUG
#define ft_assert(pred)
#else
#define ft_assert(pred)                                                        \
	ft_assert_impl(!(!(pred)), #pred, __FUNCTION__, __FILE__, __LINE__)
#endif

#define eprint(...) ft_dprintf(STDERR_FILENO, __VA_ARGS__)


//4096

struct memhdr {
	size_t pinuse : 1;
	size_t cinuse : 1;
	size_t mapped : 1;
	size_t size : 61;
};

struct memftr {
	size_t size;
};

struct freehdr {
	struct memhdr base;
	struct freehdr *next;
	struct freehdr *prev;
};

static struct {
	size_t pagesize;
	struct memhdr *small; //TODO remove
	struct freehdr *small_top;

	struct freehdr *small_bins[SMALLBIN_COUNT];
} mstate;

static void dump();

static void ft_assert_impl(int pred, const char *predstr, const char *func,
		    const char *file, int line)
{
	if (!pred) {
		ft_dprintf(STDERR_FILENO, "%s:%i: %s: Assertion '%s' failed.\n",
			   file, line, func, predstr);
		dump();
		abort();
	}
}

static struct memftr *prevftr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	ft_assert(!hdr->pinuse);

	return (struct memftr*) ((char*) chunk - sizeof(struct memhdr));
}

static struct memhdr *prevhdr(const void *chunk)
{
	return (struct memhdr*) ((char *)chunk - prevftr(chunk)->size - sizeof(struct memhdr));
}

static struct memhdr *nexthdr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr *)chunk;
	return (struct memhdr*) ((char*) chunk + hdr->size + sizeof(struct memhdr));
}

static struct memftr *getftr(const void *chunk)
{
	struct memhdr *hdr = (struct memhdr*)chunk;
	ft_assert(!hdr->cinuse);
	return (struct memftr*) ((char*) chunk + hdr->size + sizeof(struct memhdr) - sizeof(struct memftr));
}

static void setsize(void *chunk, size_t n)
{
	struct memhdr *hdr = (struct memhdr *)chunk;

	ft_assert(n + sizeof(struct memhdr) >= MIN_CHUNK_SIZE);
	ft_assert(!hdr->cinuse);

	hdr->size = n;
	getftr(chunk)->size = n;
}

static void setinuse(void *chunk, bool val)
{
	struct memhdr *hdr = (struct memhdr *)chunk;

	hdr->cinuse = nexthdr(chunk)->pinuse = val;
}

static void* chunk2mem(const void *chunk)
{
	return (char*) chunk + sizeof(struct memhdr);
}

static struct memhdr* mem2chunk(const void *p)
{
	return (struct memhdr*) ((char *) p - sizeof(struct memhdr));
}

static size_t small_binidx(size_t size)
{
	ft_assert(size >= 24);
	size -= 8;
	ft_assert(IS_ALGINED_TO(size, 16));
	size_t idx = size / 16;
	return idx - 1;
}

static void assert_correct_freelist(const struct freehdr *hdr, size_t size)
{
	const struct freehdr *cur = hdr;
	if (!cur)
		return;
	do {
		ft_assert(!cur->base.cinuse);
		ft_assert(cur->base.pinuse);
		ft_assert(nexthdr(cur)->cinuse);
		ft_assert(cur->base.size == getftr(cur)->size);
		if (size != 0)
			ft_assert(cur->base.size == size);

		ft_assert(cur->next);
		ft_assert(cur->prev);

		ft_assert(cur->prev->next == cur);
		ft_assert(cur->next->prev == cur);

		cur = cur->next;
	} while (cur != hdr);
}

static void assert_correct_small(void)
{
	const struct memhdr *cur = mstate.small;
	if (!cur)
		return;

	const struct memhdr *prev = NULL;
	do {
		ft_assert(!(!cur->cinuse && !cur->pinuse));
		if (prev)
			ft_assert(prev->cinuse == cur->pinuse);
		prev = cur;
		cur = nexthdr(cur);
	} while (cur->size != 0);
}

static void assert_correct(void)
{
	assert_correct_small();
	assert_correct_freelist(mstate.small_top, 0);

	size_t size = 24;
	for (size_t i = 0; i < sizeof(mstate.small_bins)/sizeof(mstate.small_bins[0]); ++i) {
		assert_correct_freelist(mstate.small_bins[i], size);
		size += 16;
	}
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

		eprint(" %p: %p - %p: ", cur, chunk2mem(cur), (char*) chunk2mem(cur) + cur->size);
		eprint(" size=%#.6zx mapped=%i pinuse=%i", cur->size, cur->mapped, cur->pinuse);

		if (!cur->cinuse) {
			const struct freehdr* hdr = (const struct freehdr*) cur;
			eprint(" next=%p prev=%p", hdr->next, hdr->prev); 

			if (cur->size <= MAX_SMALL_SIZE)
				eprint(" bin=%zu", small_binidx(cur->size));
		}

		if (!cur->pinuse) {
			eprint(" prevhdr=%p", (void*) prevhdr(cur));
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

static void unlink_chunk(struct freehdr **start, struct freehdr *hdr)
{
	if (hdr->next == hdr) {
		*start = NULL;
		hdr->next = hdr->prev = NULL;
		return;
	}

	struct freehdr *next = hdr->next;
	struct freehdr *prev = hdr->prev;

	if (next == prev) {
		next->next = next->prev = next;
	} else {
		next->prev = prev;
		prev->next = next;
	}
	
	if (hdr == *start)
		*start = next;

	hdr->next = hdr->prev = NULL;
}

static void append_chunk(struct freehdr **list, struct freehdr *chunk)
{
	ft_assert(!chunk->base.cinuse);
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

static void append_small_chunk(struct freehdr *chunk)
{
	struct freehdr **list = &mstate.small_top;
	if (chunk->base.size <= MAX_SMALL_SIZE) {
		size_t bin = small_binidx(chunk->base.size);
		list = &mstate.small_bins[bin];
	}

	append_chunk(list, chunk);
}

static void* split_chunk(struct freehdr *chunk, size_t n)
{
	size_t rem = chunk->base.size - n;

	ft_assert(rem >= MIN_CHUNK_SIZE);

	setsize(chunk, n);

	struct freehdr *next = (struct freehdr*) nexthdr(chunk);
	setsize(next, rem - sizeof(struct memhdr));
	setinuse(next, false);

	chunk->next = chunk->prev = next->next = next->prev = NULL;
	return next;
}

static struct freehdr *find_bestfit(struct freehdr *list, size_t n)
{
	struct freehdr *cur = list;
	struct freehdr *best = NULL;

	if (!list)
		return NULL;

	do {
		if (cur->base.size == n) {
			return cur;
		} else if (cur->base.size > n) {
			if (!best || cur->base.size < best->base.size)
				best = cur;
		}
		cur = cur->next;
	} while (cur != list);
	return best;
}

static struct freehdr **find_bin(size_t n)
{
	size_t bin = small_binidx(n);

	struct freehdr **list = &mstate.small_bins[bin];
	while (bin < SMALLBIN_COUNT) {
		if (*list) {
			return list;
		} else {
			bin += 1;
			list = &mstate.small_bins[bin];
		}
	}
	return NULL;
}

static struct freehdr *malloc_small_from_bins(size_t n)
{
	struct freehdr **list = find_bin(n);
	if (list) {
		struct freehdr *chunk = *list;

		unlink_chunk(list, chunk);
		if (chunk->base.size != n && chunk->base.size - n - sizeof(struct memhdr) >= MIN_CHUNK_SIZE) {
			struct freehdr *rem = split_chunk(chunk, n);
			append_small_chunk(rem);
		}
		return chunk;
	}
	return NULL;
}

static struct freehdr *alloc_chunk(size_t minsize)
{
	size_t padding = 4 * sizeof(struct memhdr) + (MALLOC_ALIGN - 2 * sizeof(struct memhdr));

	size_t size = ROUND_UP(minsize + padding, mstate.pagesize);

	struct freehdr *chunk =
	    mmap(NULL, size,
		 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (chunk != MAP_FAILED) {
		chunk->base.size = 0;
		chunk->base.pinuse = true;
		setinuse(chunk, true);

		chunk = (struct freehdr*) nexthdr(chunk);

		chunk->next = chunk->prev = NULL;
		chunk->base.pinuse = true;
		chunk->base.cinuse = false;

		size_t chunksize = (size - padding) | 8;
		//ft_assert(IS_ALGINED_TO(chunksize, MALLOC_ALIGN));
		setsize(chunk, chunksize);

		nexthdr(chunk)->size = 0;
		nexthdr(chunk)->cinuse = true;
		nexthdr(chunk)->pinuse = false;
	}
	return chunk;
}

static struct freehdr *malloc_small_new(size_t n)
{
	struct freehdr *chunk = find_bestfit(mstate.small_top, n);

	if (!chunk || chunk->base.size - n < MIN_CHUNK_SIZE) {
		chunk = alloc_chunk(MAX_SMALL_SIZE * SMALL_COUNT);
		if (!chunk)
			return NULL; //TODO enomem
		append_chunk(&mstate.small_top, chunk);

		if (!mstate.small)
			mstate.small = &chunk->base;
	}

	//TODO make sure that remaining chunk is also aligned
	unlink_chunk(&mstate.small_top, chunk);
	if (chunk->base.size - n >= MIN_CHUNK_SIZE) {
		struct freehdr *rem = split_chunk(chunk, n);
		append_small_chunk(rem);
	}
	return chunk;
}

static struct freehdr *malloc_small(size_t n)
{
	if (n < MIN_ALLOC_SIZE) {
		n = MIN_ALLOC_SIZE;
	} else {
		n = ROUND_UP(n, sizeof(struct memhdr));
		if (IS_ALGINED_TO(n, MALLOC_ALIGN))
			n += sizeof(struct memhdr);
	}

	struct freehdr *chunk = malloc_small_from_bins(n);

	if(!chunk)
		chunk = malloc_small_new(n);

	return chunk;
}

static void init_malloc(void)
{
	mstate.small = NULL;
	mstate.small_top = NULL;
	mstate.pagesize = getpagesize();

	for (size_t bin = 0; bin < sizeof(mstate.small_bins)/sizeof(mstate.small_bins[0]); ++bin)
		mstate.small_bins[bin] = NULL;
}

void* ft_malloc(size_t n)
{
	static bool inited = false;
	if (!inited) {
		inited = true;

		init_malloc();
	}

	if (!n) 
		return NULL;

	struct freehdr *chunk = NULL;
	//TODO lock
	assert_correct();
	if (n <= MAX_SMALL_SIZE)
		chunk = malloc_small(n);
	else
		ft_assert(0);

	setinuse(chunk, true);

	assert_correct();
	//TODO unlock

	void *p = chunk2mem(chunk);
	ft_assert(IS_ALGINED_TO(p, MALLOC_ALIGN));
	return p;
}

static struct memhdr* merge_chunks(struct memhdr *a, struct memhdr *b)
{
	ft_assert(a < b);

	size_t new_size = a->size + b->size + sizeof(struct memhdr);

	setsize(a, new_size);
	return a;
}

static void free_small(struct memhdr *chunk)
{
	struct freehdr *hdr = (struct freehdr*) chunk;
	hdr->next = hdr->prev = NULL;

	if (!chunk->pinuse) {
		struct memhdr *prev = prevhdr(chunk);
		if (prev->size <= MAX_SMALL_SIZE) {
			size_t bin = small_binidx(prev->size);
			unlink_chunk(&mstate.small_bins[bin], (struct freehdr*) prev);
		} else {
			unlink_chunk(&mstate.small_top, (struct freehdr*) prev);
		}
		chunk = merge_chunks(prev, chunk);
	}

	struct memhdr *next = nexthdr(chunk);
	if (!next->cinuse) {
		if (next->size <= MAX_SMALL_SIZE) {
			size_t bin = small_binidx(next->size);
			unlink_chunk(&mstate.small_bins[bin], (struct freehdr*) next);
		} else {
			unlink_chunk(&mstate.small_top, (struct freehdr*) next);
		}
		chunk = merge_chunks(chunk, next);
	}

	append_small_chunk((struct freehdr*) chunk);
}

void ft_free(void *p)
{
	if (!p)
		return;

	if (!IS_ALGINED_TO(p, MALLOC_ALIGN)) {
		eprint("free(): invalid pointer\n");
		return;
	}

	struct memhdr* chunk = mem2chunk(p);

	//TODO lock
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
	//TODO unlock

}

void show_alloc_mem(void)
{
	dump();
	eprint("\n");
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

void *ft_realloc(void *userptr, size_t size)
{
#ifdef TRACES
	ft_dprintf(STDERR_FILENO, "//ft_realloc(%p, %zu);\n", userptr, size);
#endif
	void *res = ft_malloc(size);
	if (res) {
		if (userptr) {
			struct memhdr *hdr = mem2chunk(userptr);
			ft_memcpy(res, userptr, size > hdr->size ? hdr->size : size);
		}
		ft_free(userptr);
	}

	return res;
}

void *ft_aligned_alloc(size_t align, size_t size)
{
	ft_assert(0 && "todo");
	//TODO THIS IS VERY WRONG!
	return ft_malloc(ROUND_UP(size, align));
}

size_t ft_malloc_usable_size(void *userptr)
{
	if (!userptr)
		return 0;
	return mem2chunk(userptr)->size;
}

void *ft_memalign(size_t align, size_t size)
{
	(void)align;
	(void)size;
	ft_assert(0);
	return NULL;
}

void *ft_valloc(size_t size)
{
	(void)size;
	ft_assert(0);
	return NULL;
}

void *ft_pvalloc(size_t size)
{
	(void) size;
	ft_assert(0);
	return NULL;
}

