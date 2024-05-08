#include "malloc.h"

#include <stdbool.h>

// TODO REMOVE
#include <stdlib.h>
#include <stdio.h>

#include <ft/stdio.h>
#include <ft/string.h>

#include <sys/mman.h>
#include <sys/param.h>
#include <unistd.h>
#include <pthread.h>

#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

#define MALLOC_ALIGN 16
#define MIN_SMALLCHUNK_SIZE 16
#define MIN_LARGECHUNK_SIZE 32

#define SMALL_COUNT 128
#define SMALL_MAX_SIZE 128
#define SMALL_SIZE (SMALL_COUNT * SMALL_MAX_SIZE)
#define SMALL_MIN_ALLOC_SIZE 8
#define LARGE_COUNT 128
#define LARGE_MAX_SIZE 16384
#define LARGE_MIN_ALLOC_SIZE 24
#define LARGE_SIZE (LARGE_COUNT * LARGE_MAX_SIZE)

#define ROUND_UP(x, boundary) ((x + boundary - 1) & ~(boundary - 1))
#define IS_ALGINED_TO(x, boundary) ((x & (boundary - 1)) == 0)

#define ft_assert(pred)                                                        \
	ft_assert_impl(!(!(pred)), #pred, __FUNCTION__, __FILE__, __LINE__)

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

static struct {
	struct mem_hdr *small_start;
	struct large_hdr *large_start;
	struct large_hdr *large_next_free;
	pthread_mutex_t mtx;
} state;

static bool is_sentinel(const void *chunk);
static struct mem_hdr *nexthdr(const void *chunk);
static void *get_userptr_raw(const void *chunk);

static void dump_small(void)
{
	const struct mem_hdr *cur = state.small_start;

	if (!cur) {
		ft_dprintf(STDERR_FILENO, "\033[34mNO SMALL MEMORY!!\033[m\n");
		return;
	}

	while (!is_sentinel(cur)) {
		size_t size = cur->size;
		if (cur->cinuse)
			ft_dprintf(STDERR_FILENO, "\033[31m");
		else
			ft_dprintf(STDERR_FILENO, "\033[32m");

		ft_dprintf(STDERR_FILENO, "%p: P=%i C=%i %p - %p : %7zu\033[m\n", cur, cur->pinuse,
		       cur->cinuse, get_userptr_raw(cur), get_userptr_raw(cur) + size,
		       size);

		cur = nexthdr(cur);
	}
}

static void dump_large_one(const struct large_hdr *hdr)
{
	size_t size = hdr->base.size;
	if (hdr->base.cinuse)
		ft_dprintf(STDERR_FILENO, "\033[31m");
	else
		ft_dprintf(STDERR_FILENO, "\033[32m");

	ft_dprintf(STDERR_FILENO, "%p: P=%i C=%i total=%7zu %p - %p : %7zu", hdr,
		   hdr->base.pinuse, hdr->base.cinuse, size + sizeof(struct large_hdr), get_userptr_raw(hdr),
		   get_userptr_raw(hdr) + size, size);

	if (!hdr->base.cinuse)
		ft_dprintf(STDERR_FILENO, " prev_free=%p next_free=%p",
			   hdr->prev_free, hdr->next_free);
	ft_dprintf(STDERR_FILENO, "\033[m\n");
}

static void dump_large(void)
{
	const struct large_hdr *cur = state.large_start;
	if (!cur) {
		ft_dprintf(STDERR_FILENO, "\033[34mNO LARGE MEMORY!!\033[m\n");
		return;
	}

	while (!is_sentinel(cur)) {
		dump_large_one(cur);
		cur = (struct large_hdr*) nexthdr(cur);
	}
}

static void dump(void)
{
	ft_dprintf(STDERR_FILENO, "SMALL: %p - %p\n", state.small_start, (char*) state.small_start + SMALL_SIZE);
	dump_small();

	ft_dprintf(STDERR_FILENO, "LARGE: %p - %p\n", state.large_start, (char*) state.large_start + LARGE_SIZE);
	dump_large();
}

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

static void *get_userptr_raw(const void *chunk)
{
	return (char *)chunk + sizeof(struct mem_hdr);
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

static void assert_small_correct(void)
{
	struct mem_hdr *cur = state.small_start;

	if (!cur)
		return;

	struct mem_hdr *prev = NULL;

	while ((char*)cur < (char*)state.small_start + SMALL_SIZE) {
		if ((char*)cur < (char*)state.small_start + SMALL_SIZE - sizeof(struct mem_hdr))
			ft_assert(!is_sentinel(cur));

		if (prev)
			ft_assert(cur->pinuse == prev->cinuse);

		prev = cur;
		cur = nexthdr(cur);
	}
}

static void assert_large_correct(void)
{
	if (!state.large_start)
		return;

	struct large_hdr *cur = state.large_start;
	struct large_hdr* prev = NULL;

	while ((char*)cur < (char*)state.large_start + LARGE_SIZE) {
		if (!cur->base.cinuse) {
			ft_assert(cur->prev_free != NULL);
			ft_assert(cur->next_free != NULL);

			ft_assert(cur->prev_free->next_free == cur);
			ft_assert(cur->next_free->prev_free == cur);
		} 

		if ((char*)cur < (char*)state.large_start + LARGE_SIZE - sizeof(struct mem_hdr)) {
			ft_assert(cur->base.size >= LARGE_MIN_ALLOC_SIZE);
			ft_assert(!is_sentinel(cur));
		}

		if (prev)
			ft_assert(cur->base.pinuse == prev->base.cinuse);

		prev = cur;
		cur = (struct large_hdr *) nexthdr(cur);
	}

	cur = state.large_next_free;
	if (!cur)
		return;

	do {
		ft_assert(cur->prev_free != NULL);
		ft_assert(cur->next_free != NULL);
		ft_assert(!cur->base.cinuse);

		cur = cur->next_free;
	} while (cur != state.large_next_free);
}

static void assert_correct(void)
{
	assert_small_correct();
	assert_large_correct();
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
	hdr->size = n;
	get_ftr(chunk)->size = n;
}

static void split_small(struct mem_hdr *chunk, size_t n)
{
	size_t oldsize = chunk->size;
	set_size(chunk, n);

	struct mem_hdr *next = nexthdr(chunk);
	next->pinuse = next->cinuse = 0; // has to happen before set_size, as
					 // there is an assert in it that check
					 // if it's allocated or not
	set_size(next, oldsize - n - sizeof(struct mem_hdr));
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
	ft_assert(!chunk->base.cinuse && "tried to append allocated chunk to free list");
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
	ft_assert(!chunk->base.cinuse);
	ft_assert(!next->base.cinuse);
	ft_assert(!next->base.pinuse);
	//op is hetzelfde als link verwijdern en tweede inserten in middle
	
	if (chunk->next_free == chunk) {
		//free list of size 1
		next->next_free = next;
		next->prev_free = next;
	} else {
		next->prev_free = chunk->prev_free;
		next->next_free = chunk->next_free;
		chunk->prev_free->next_free = next;
		chunk->next_free->prev_free = next;
	}

	if (state.large_next_free == chunk)
		state.large_next_free = next;

	chunk->next_free = chunk->prev_free = NULL; //TODO remove
}

static void unlink_large(struct large_hdr *chunk)
{
	if (chunk->next_free == chunk) {
		//only link in free list
		state.large_next_free = NULL;
		return;
	}

	struct large_hdr *next = chunk->next_free;
	struct large_hdr *prev = chunk->prev_free;

	if (next == prev) {
		next->next_free = next->prev_free = next;
	} else {
		next->prev_free = prev;
		prev->next_free = next;
	}

	if (state.large_next_free == chunk)
		state.large_next_free = next;

	chunk->next_free = chunk->prev_free = NULL; //TODO remove
}

static void *malloc_large(size_t n)
{
	if (n < LARGE_MIN_ALLOC_SIZE)
		n = LARGE_MIN_ALLOC_SIZE;
	struct large_hdr *chunk = find_bestfit_large(n);
	if (!chunk) {
		more_large();
		chunk = find_bestfit_large(n);

		ft_assert(chunk && "this should never happen");
	}

	if (chunk->base.size - n >= LARGE_MIN_ALLOC_SIZE + sizeof(struct mem_hdr))
		split_large_unlink_first(chunk, n);
	else
		unlink_large(chunk);
	set_inuse(chunk, true);

	assert_correct();
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
	pthread_mutex_init(&state.mtx, NULL);
	init_malloc_small();
	init_malloc_large();
}

void *ft_malloc(size_t n)
{
#ifdef TRACES
	ft_dprintf(STDERR_FILENO, "//ft_malloc(%zu);\n", n);
#endif
	size_t old_n = n;


	static bool initialized = false;
	if (!initialized) {
		init_malloc();
		initialized = true;
	}

	if (n == 0)
		return NULL;

	int rc = pthread_mutex_lock(&state.mtx);
	if (rc)
		return NULL;
	assert_correct();

	n = ROUND_UP(n, MALLOC_ALIGN);

	void *res = NULL;
	if (n <= SMALL_MAX_SIZE)
		res = try_malloc_small(n);

	if (!res && n <= LARGE_MAX_SIZE)
		res = malloc_large(n);
	
	if (n > LARGE_MAX_SIZE)
		res = malloc_huge(n);

	ft_assert(res && "this should probably not happen");
#ifdef TRACES
	ft_dprintf(STDERR_FILENO, "void *tmp_%p = ft_malloc(%zu);\n", res, old_n);
#endif
	assert_correct();

#if __has_feature(address_sanitizer)
	__asan_poison_memory_region(res, n);
#endif

	rc = pthread_mutex_unlock(&state.mtx);
	ft_assert(rc == 0);

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

static void free_common(struct mem_hdr *chunk)
{
	ft_assert(chunk->cinuse && "double free" && chunk);
	set_inuse(chunk, false);
	get_ftr(chunk)->size = chunk->size;
}

static void free_small(struct mem_hdr *chunk)
{
	free_common(chunk);
	if (!chunk->pinuse)
		chunk = merge_chunks(prevhdr(chunk), chunk);

	struct mem_hdr *next = nexthdr(chunk);
	if (!next->cinuse)
		merge_chunks(chunk, next);
}

static struct large_hdr* merge_large(struct large_hdr *new, struct large_hdr *old)
{
	struct large_hdr *first = new < old ? new : old;
	struct large_hdr *second = new < old ? old : new;
	merge_chunks((struct mem_hdr*)first, (struct mem_hdr*)second);

	if (old->next_free == old) {
		//free list only has one chunk
		state.large_next_free = first;
		first->next_free = first;
		first->prev_free = first;
		return first;
	}

	//free list is at least of size 2
	//TODO probably a good idea to do lists properly with their own
	//functions instead of hacking it in
	if (new < old) {
		if (new->next_free == old && new->prev_free == old) {
			//free list is of size 2
			new->next_free = new->prev_free = new;

		} else if (new->next_free) {
			unlink_large(old);
		} else {
			new->prev_free = old->prev_free;
			new->next_free = old->next_free;

			old->next_free->prev_free = new;
			old->prev_free->next_free = new;
		}

		if (state.large_next_free == old)
			state.large_next_free = old->next_free;
	}
	return first;
}

static void free_large(struct mem_hdr *chunk)
{
	free_common(chunk);

	struct large_hdr *hdr = (struct large_hdr *)chunk;
	hdr->next_free = hdr->prev_free = NULL;

	if (!chunk->pinuse) {
		hdr = merge_large(hdr, (struct large_hdr*) prevhdr(chunk));
		assert_correct();
	}
	
	struct large_hdr *next = (struct large_hdr *)nexthdr(chunk);
	if (!next->base.cinuse) {
		hdr = merge_large(hdr, next);
		assert_correct();
	}

	if (!hdr->next_free)
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
#ifdef TRACES
	ft_dprintf(STDERR_FILENO, "ft_free(tmp_%p);\n", userptr);
#endif

	if (!userptr)
		return;

	int rc = pthread_mutex_lock(&state.mtx);
	ft_assert(rc == 0);

	assert_correct();

	struct mem_hdr *chunk = get_chunkptr(userptr);
#if __has_feature(address_sanitizer)
	__asan_unpoison_memory_region(userptr, chunk->size);
#endif
	if (is_small(chunk))
		free_small(chunk);
	else if (!is_huge(chunk))
		free_large(chunk);
	else
		free_huge(chunk);
	assert_correct();

	rc = pthread_mutex_unlock(&state.mtx);
	ft_assert(rc == 0);
}

static void show_alloc_mem_start(const void *chunk)
{
	const struct mem_hdr *cur = chunk;

	while (!is_sentinel(cur)) {
		if (cur->cinuse)
			ft_printf("%p - %p : %zu bytes\n", (void *)cur,
				  (char *)cur + cur->size, (size_t) cur->size);
		cur = nexthdr(cur);
	}
}

static void show_large_info(void)
{
	struct large_hdr *cur = state.large_start;

	if (!cur)
		return;

	while (!is_sentinel(cur)) {
		dump_large_one(cur);

		cur = (struct large_hdr*) nexthdr(cur);
	}
	ft_printf("\n");
}

void show_alloc_mem(void)
{
	dump();

	/*ft_printf("TINY : %p\n", (void *)state.small_start);
	show_alloc_mem_start(state.small_start);
	ft_printf("LARGE : %p\n", (void *)state.large_start);
	show_alloc_mem_start(state.large_start);*/
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
			struct mem_hdr *hdr = get_chunkptr(userptr);
			ft_memcpy(res, userptr, size > hdr->size ? hdr->size : size);
		}
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

