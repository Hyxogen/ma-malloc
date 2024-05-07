#include "malloc.h"

#include <stdbool.h>

// TODO REMOVE
#include <stdlib.h>
#include <stdio.h>

#include <ft/stdio.h>
#include <ft/string.h>

#include <sys/mman.h>
#include <unistd.h>

#define MALLOC_ALIGN 8
#define MIN_SMALLCHUNK_SIZE 16
#define MIN_LARGECHUNK_SIZE 32

#define SMALL_COUNT 128
#define SMALL_MAX_SIZE 128
#define SMALL_SIZE (SMALL_COUNT * SMALL_MAX_SIZE)
#define LARGE_COUNT 128
#define LARGE_MAX_SIZE 16384
#define LARGE_SIZE (LARGE_COUNT * LARGE_MAX_SIZE)

#define ROUND_UP(x, boundary) ((x + boundary - 1) & ~(boundary - 1))
#define IS_ALGINED_TO(x, boundary) ((x & (boundary - 1)) == 0)

#define ft_assert(pred)                                                        \
	ft_assert_impl((pred), #pred, __FUNCTION__, __FILE__, __LINE__)

#ifndef USE_FT_PREFIX
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

struct mem_hdr {
	size_t pinuse : 1;
	size_t cinuse : 1;
	size_t size : 62;
};

struct mem_ftr {
	size_t size;
};

struct large_hdr {
	struct mem_hdr base;
	struct large_hdr *next_free;
	struct large_hdr *prev_free;
};

struct {
	struct mem_hdr *small_start;
	struct large_hdr *large_start;
	struct large_hdr *large_next_free;
} state;

void ft_assert_impl(int pred, const char *predstr, const char *func,
		    const char *file, int line)
{
	if (!pred) {
		ft_dprintf(STDERR_FILENO, "%s:%i: %s: Assertion '%s' failed.\n",
			   file, line, func, predstr);
		abort();
	}
}

static struct mem_hdr *nexthdr(const void *chunk)
{
	const struct mem_hdr *hdr = (const struct mem_hdr *)chunk;
	return (struct mem_hdr *)((char *)chunk + hdr->size +
				  sizeof(struct mem_hdr));
}

static struct mem_ftr *prevftr(const void *chunk)
{
	const struct mem_hdr *hdr = (const struct mem_hdr *)chunk;
	ft_assert(!hdr->pinuse && "no footer present on allocated chunk");
	return (struct mem_ftr *)((char *)chunk - sizeof(struct mem_ftr));
}

static struct mem_hdr *prevhdr(const void *chunk)
{
	const struct mem_ftr *ftr = prevftr(chunk);
	return (struct mem_hdr *)((char *)chunk - ftr->size -
				  sizeof(struct mem_hdr));
}

static struct mem_ftr *get_ftr(const void *chunk)
{
	return (struct mem_ftr *) ((char *) nexthdr(chunk) - sizeof(struct mem_ftr));
}

static void *get_userptr(const void *chunk)
{
	const struct mem_hdr *hdr = (const struct mem_hdr *)chunk;
	ft_assert(hdr->cinuse && "tried to get userptr of a freed chunk");
	return (char *)chunk + sizeof(struct mem_hdr);
}

static struct mem_hdr *get_chunkptr(const void *userptr)
{
	return (struct mem_hdr *)((char *)userptr - sizeof(struct mem_hdr));
}

static bool is_sentinel(const void *chunk)
{
	const struct mem_hdr *hdr = (const struct mem_hdr *)chunk;
	return hdr->size == 0;
}

static struct mem_hdr *find_bestfit_small(size_t n)
{
	struct mem_hdr *cur = state.small_start;
	struct mem_hdr *best = NULL;

	while (!is_sentinel(cur)) {
		if (!cur->cinuse && cur->size >= n) {
			if (cur->size == n)
				return cur;

			if (!best)
				best = cur;
			else if (cur->size < best->size)
				best = cur;
		}
		cur = nexthdr(cur);
	}
	return best;
}

static void set_size(void *chunk, size_t n)
{
	struct mem_hdr *hdr = (struct mem_hdr *)chunk;
	hdr->size = get_ftr(chunk)->size = n;
}

static void split_small(struct mem_hdr *chunk, size_t n)
{
	size_t oldsize = chunk->size;
	set_size(chunk, n);

	struct mem_hdr *next = nexthdr(chunk);
	set_size(next, oldsize - n - sizeof(struct mem_hdr));
	next->pinuse = next->cinuse = 0;
}

static void set_inuse(void *chunk, bool used)
{
	struct mem_hdr *hdr = (struct mem_hdr *)chunk;
	struct mem_hdr *next = nexthdr(chunk);

	hdr->cinuse = next->pinuse = used;
}

static void *try_malloc_small(size_t n)
{
	struct mem_hdr *chunk = find_bestfit_small(n);
	if (!chunk)
		return NULL;

	if (chunk->size - n >= MIN_SMALLCHUNK_SIZE)
		split_small(chunk, n);

	set_inuse(chunk, true);
	return get_userptr(chunk);
}

static struct large_hdr *find_bestfit_large(size_t n)
{
	struct large_hdr *cur = state.large_next_free;
	struct large_hdr *best = NULL;

	if (!cur)
		return NULL;

	do {
		ft_assert(!cur->base.cinuse && "this should not happen");
		if (cur->base.size == n)
			return cur;

		if (cur->base.size >= n) {
			if (!best)
				best = cur;
			else if (cur->base.size < best->base.size)
				best = cur;
		}

		cur = cur->next_free;
	} while (cur != state.large_next_free);
	return best;
}

static void init_chunk(void *chunk, size_t size)
{
	struct mem_hdr *hdr = (struct mem_hdr *)chunk;

	size_t s = size - 2 * sizeof(struct mem_hdr);
	set_size(hdr, s);
	hdr->cinuse = false;
	hdr->pinuse = true;

	struct mem_hdr *sentinel = nexthdr(hdr);
	sentinel->size = 0;
	sentinel->pinuse = false;
	sentinel->cinuse = true;
}

static void append_large(struct large_hdr *chunk)
{
	if (!state.large_next_free) {
		state.large_next_free = chunk;
		chunk->next_free = chunk;
		chunk->prev_free = chunk;
		return;
	}

	chunk->prev_free = state.large_next_free->prev_free;
	chunk->next_free = state.large_next_free;

	state.large_next_free->prev_free->next_free = chunk;
	state.large_next_free->prev_free = chunk;
}

static void more_large(void)
{
	struct large_hdr *chunk = mmap(NULL, LARGE_SIZE, PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	ft_assert(chunk != MAP_FAILED);
	init_chunk(chunk, LARGE_SIZE);
	append_large(chunk);
}

static void split_large_unlink_first(struct large_hdr *chunk, size_t n)
{
	split_small((struct mem_hdr *)chunk, n);

	struct large_hdr *next = (struct large_hdr *)nexthdr(chunk);

	if (state.large_next_free == chunk)
		state.large_next_free = next;

	if (chunk->prev_free == chunk) {
		next->next_free = next->prev_free = next;
	} else {
		next->prev_free = chunk->prev_free;
		next->next_free = chunk->next_free;
	}
}

static void *malloc_large(size_t n)
{
	struct large_hdr *chunk = find_bestfit_large(n);
	if (!chunk) {
		more_large();
		chunk = find_bestfit_large(n);

		ft_assert(chunk && "this should never happen");
	}

	if (chunk->base.size - n >= MIN_LARGECHUNK_SIZE)
		split_large_unlink_first(chunk, n);
	set_inuse(chunk, true);
	return get_userptr(chunk);
}

static void *malloc_huge(size_t n)
{
	struct mem_hdr *chunk =
	    mmap(NULL, n + sizeof(struct mem_hdr), PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	chunk->size = n;
	chunk->pinuse = chunk->cinuse = 1;
	return get_userptr(chunk);
}

static void init_malloc_small(void)
{
	state.small_start = mmap(NULL, SMALL_SIZE, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	ft_assert(state.small_start != MAP_FAILED);

	init_chunk(state.small_start, SMALL_SIZE);
}

static void init_malloc_large(void)
{
	more_large();
	state.large_start = state.large_next_free;
}

static void init_malloc(void)
{
	init_malloc_small();
	init_malloc_large();
}

void *ft_malloc(size_t n)
{
	static bool initialized = false;
	if (!initialized) {
		init_malloc();
		initialized = true;
	}

	if (n == 0)
		return NULL;

	n = ROUND_UP(n, MALLOC_ALIGN);

	void *res = NULL;
	if (n <= SMALL_MAX_SIZE)
		res = try_malloc_small(n);

	if (!res && n <= LARGE_MAX_SIZE)
		res = malloc_large(n);
	
	if (n > LARGE_MAX_SIZE)
		res = malloc_huge(n);

	return res;
}

static bool is_small(const void *chunk)
{
	return chunk >= (void *)state.small_start &&
	       chunk <= (void *)((char *)state.small_start + SMALL_SIZE);
}

static bool is_huge(const void *chunk)
{
	const struct mem_hdr *hdr = (const struct mem_hdr *)chunk;
	return hdr->size > LARGE_MAX_SIZE;
}

static struct mem_hdr *merge_chunks(struct mem_hdr *first,
				    struct mem_hdr *second)
{
	ft_assert(first < second);

	size_t new_size = first->size + second->size + sizeof(struct mem_hdr);
	set_size(first, new_size);

	return first;
}

static void free_small(struct mem_hdr *chunk)
{
	set_inuse(chunk, false);
	if (!chunk->pinuse)
		chunk = merge_chunks(prevhdr(chunk), chunk);

	struct mem_hdr *next = nexthdr(chunk);
	if (!next->cinuse)
		merge_chunks(chunk, next);
}

static void free_large(struct mem_hdr *chunk)
{
	struct large_hdr *hdr = (struct large_hdr *)chunk;
	set_inuse(chunk, false);

	bool merged = false;
	if (!chunk->pinuse) {
		chunk = merge_chunks(prevhdr(chunk), chunk);
		merged = true;
	}

	struct large_hdr *next = (struct large_hdr *)nexthdr(chunk);
	if (!next->base.cinuse) {
		next->prev_free->next_free = next->next_free->prev_free = hdr;
		merge_chunks(chunk, (struct mem_hdr *)next);
		merged = true;
	}

	if (!merged)
		append_large(hdr);
}

static void free_huge(struct mem_hdr *chunk)
{
	int rc = munmap(chunk, chunk->size + sizeof(struct mem_hdr));

	if (rc != 0)
		perror("munmap");
}

void ft_free(void *userptr)
{
	if (!userptr)
		return;

	struct mem_hdr *chunk = get_chunkptr(userptr);
	if (is_small(chunk))
		free_small(chunk);
	else if (!is_huge(chunk))
		free_large(chunk);
	else
		free_huge(chunk);
}

static void show_alloc_mem_start(const void *chunk)
{
	const struct mem_hdr *cur = chunk;

	while (!is_sentinel(cur)) {
		if (cur->cinuse)
			ft_printf("%p - %p : %zu bytes\n", (void *)cur,
				  (char *)cur + cur->size, cur->size);
		cur = nexthdr(cur);
	}
}

void show_alloc_mem(void)
{
	ft_printf("TINY : %p\n", (void *)state.small_start);
	show_alloc_mem_start(state.small_start);
	ft_printf("LARGE : %p\n", (void *)state.large_start);
	show_alloc_mem_start(state.large_start);
}

void *ft_calloc(size_t nmemb, size_t size)
{
	size_t n = nmemb * size;

	if (nmemb && n / nmemb != size)
		return NULL;

	void *res = ft_malloc(n);
	ft_memset(res, 0, size);

	return res;
}

void *ft_realloc(void *userptr, size_t size)
{
	void *res = ft_malloc(size);
	if (res && userptr) {
		struct mem_hdr *hdr = get_chunkptr(userptr);
		ft_memcpy(res, userptr, size > hdr->size ? hdr->size : size);
		ft_free(userptr);
	}

	return res;
}

void *ft_aligned_alloc(size_t align, size_t size)
{
	return ft_malloc(ROUND_UP(size, align));
}

size_t ft_malloc_usable_size(void *userptr)
{
	if (!userptr)
		return 0;
	return get_chunkptr(userptr)->size;
}

void *ft_memalign(size_t align, size_t size)
{
	return ft_aligned_alloc(align, size);
}

void *ft_valloc(size_t size)
{
	int pagesize = getpagesize();
	return ft_aligned_alloc(pagesize, size);
}

void *ft_pvalloc(size_t size)
{
	(void) size;
	ft_assert(0);
}

