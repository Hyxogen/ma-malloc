#include "malloc.h"

#include <stdbool.h>
#include <errno.h>

//TODO REMOVE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <ft/stdio.h>

#include <unistd.h>
#include <sys/mman.h>

#define MALLOC_ALIGN 8
#define TINY_COUNT 128
#define TINY_MAX_SIZE 128
#define SMALL_MAX_SIZE 16384

#define TINY_MIN_SIZE (sizeof(struct hdr_base) + sizeof(struct chunk_ftr) + 8)
#define SMALL_MIN_SIZE (sizeof(struct small_chunk_hdr) + sizeof(struct chunk_ftr) + 8)

#define ROUND_UP(x, boundary) ((x + boundary - 1) & ~(boundary - 1))
#define IS_ALGINED_TO(x, boundary) ((x & (boundary - 1)) == 0)

#define ft_assert(pred) ft_assert_impl((pred), #pred, __FILE__, __LINE__)

struct hdr_base {
	size_t pused : 1;
	size_t cused : 1;
	size_t size : 62;
};

struct small_chunk_hdr {
	struct hdr_base base;
	struct small_chunk_hdr *next_free;
	struct small_chunk_hdr *prev_free;
};

struct chunk_ftr {
	size_t size;
};

static struct malloc_state {
	struct hdr_base *tiny;
	struct small_chunk_hdr *small;
	int pagesize;
} state;

static void ft_assert_impl(int pred, const char *predstr, const char *file, int line)
{
	if (!pred) {
		ft_dprintf(STDERR_FILENO, "%s:%i: Assertion '%s' failed.\n", file, line, predstr);
		abort();
	}
}

static void set_hdr(void *addr, bool pused, bool cused, size_t size)
{
	struct hdr_base *hdr = addr;

	hdr->pused = pused;
	hdr->cused = cused;
	hdr->size = size;
}

static struct chunk_ftr *get_ftr(const void *chunk)
{
	const struct hdr_base *hdr = chunk;
	ft_assert(!hdr->cused && "no footer present in an empty chunk");
	return (struct chunk_ftr*) ((char*) chunk + hdr->size - sizeof(struct chunk_ftr));
}

static void set_ftr(void *chunk, size_t size)
{
	struct chunk_ftr *ftr = get_ftr(chunk);
	ftr->size = size;
}

static void set_chunk(void *chunk, bool pused, bool cused, size_t size)
{
	set_hdr(chunk, pused, cused, size);
	set_ftr(chunk, size);
}

static struct small_chunk_hdr *set_small_chunk(void *chunk, bool pused, bool cused, size_t size, void *prev_free, void *next_free)
{
	set_chunk(chunk, pused, cused, size);

	struct small_chunk_hdr *hdr = chunk;
	hdr->prev_free = prev_free;
	hdr->next_free = next_free;
	return chunk;
}

static struct hdr_base *next_hdr(const void *addr)
{
	const struct hdr_base *hdr = addr;
	return (struct hdr_base *) ((char*) addr + sizeof(struct hdr_base) + hdr->size);
}

static struct small_chunk_hdr *prev_small_free(const void *chunk)
{
	const struct small_chunk_hdr *hdr = chunk;
	return hdr->prev_free;
}

static struct small_chunk_hdr *next_small_free(const void *chunk)
{
	const struct small_chunk_hdr *hdr = chunk;
	return hdr->next_free;
}

static struct hdr_base *prev_hdr(const void *addr)
{
	const struct hdr_base *hdr = addr;
	ft_assert(hdr->pused == 0 && "cannot determine previous header when in use");

	const struct chunk_ftr *ftr = (struct chunk_ftr*) ((char *) addr - sizeof(struct hdr_base));
	return (struct hdr_base*) ((char*) addr - ftr->size);
}

static struct hdr_base *get_chunkptr(const void *userptr)
{
	return (struct hdr_base*) ((char*) userptr - sizeof(struct hdr_base));
}

static void *get_userptr_beg(const void *chunk)
{
	return (char*) chunk + sizeof(struct hdr_base);
}

static void *get_userptr_end(const void *chunk)
{
	const struct hdr_base *hdr = chunk;
	return (char*) chunk + sizeof(struct hdr_base) + hdr->size;
}

static bool is_tiny(const void *chunk)
{
	return chunk >= (void*) state.tiny && chunk <= (void*) ((char*) state.tiny + TINY_COUNT * TINY_MAX_SIZE);
}

static bool is_large(void *chunk)
{
	const struct hdr_base *hdr = chunk;
	return hdr->cused == 0 && hdr->pused == 0;
}

static void init_malloc(void)
{
	state.pagesize = getpagesize();
	size_t tinysize = ROUND_UP(TINY_MAX_SIZE * TINY_COUNT, state.pagesize);
	state.tiny = mmap(NULL, tinysize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	
	if (state.tiny == MAP_FAILED)
		abort();

	set_chunk(state.tiny, true, false, tinysize - 2 * sizeof(struct hdr_base));
	set_hdr(next_hdr(state.tiny), false, true, 0);
}

static struct hdr_base* find_best_tiny(size_t n)
{
	struct hdr_base *cur = state.tiny;
	struct hdr_base *best = NULL;

	while (cur->size != 0) {
		if (!best)
			best = cur;
		else if (cur->size == n)
			return cur;
		else if (cur->size > n && cur->size < best->size)
			best = cur;
		cur = next_hdr(cur);
	}
	return best;
}

static struct small_chunk_hdr* find_best_small(size_t n)
{
	if (!state.small)
		return NULL;

	struct small_chunk_hdr *cur = state.small;
	struct small_chunk_hdr *best = NULL;

	do {
		if (!best)
			best = cur;
		else if (cur->base.size == n)
			return cur;
		else if (cur->base.size > n && cur->base.size < best->base.size)
			best = cur;
		cur = next_small_free(cur);

	} while (cur != state.small);
	return best;
}

static struct hdr_base* split_chunk_no_ftr(void *chunk, size_t new_size)
{
	struct hdr_base *hdr = chunk;

	ft_assert(hdr->size > new_size);
	ft_assert(new_size >= MALLOC_ALIGN);
	size_t rem = hdr->size - new_size - sizeof(struct hdr_base);

	ft_assert(rem > sizeof(struct hdr_base) + sizeof(struct chunk_ftr));
	ft_assert(IS_ALGINED_TO(rem, MALLOC_ALIGN));

	hdr->size = new_size;

	set_chunk(next_hdr(hdr), false, false, rem);
	return hdr;
}

static struct small_chunk_hdr *split_small_chunk_unlinked(void *chunk, size_t new_size)
{
	struct small_chunk_hdr *prev_free = prev_small_free(chunk);
	struct small_chunk_hdr *next_free = next_small_free(chunk);

	struct small_chunk_hdr *hdr = (struct small_chunk_hdr *) split_chunk_no_ftr(chunk, new_size);

	struct small_chunk_hdr *next = (struct small_chunk_hdr *) next_hdr(hdr);
	next->prev_free = prev_free;
	next->next_free = next_free;

	prev_free->next_free = next;
	next_free->prev_free = next;
}

static void set_used(void *chunk, bool v)
{
	struct hdr_base *hdr = chunk;

	next_hdr(hdr)->pused = hdr->cused = v;
}

//first should be lower addressed
static void *merge_chunks(void *first, void *second)
{
	struct hdr_base *first_hdr = first, *second_hdr = second;

	size_t new_size = first_hdr->size + second_hdr->size + sizeof(struct hdr_base);
	set_chunk(first_hdr, true, false, new_size);

	return first_hdr;
}

static void* merge_small_chunks(void *first, void *second)
{
	struct small_chunk_hdr *first_hdr = first, *second_hdr = second;

	struct small_chunk_hdr *prev_free = NULL, *next_free = NULL;
	if (!first_hdr->base.cused) {
		prev_free = first_hdr->prev_free;
		next_free = first_hdr->next_free;
	} else {
		prev_free = second_hdr->prev_free;
		next_free = second_hdr->next_free;
	}

	size_t new_size = first_hdr->base.size + second_hdr->base.size + sizeof(struct hdr_base);
	set_chunk(first_hdr, true, false, new_size);

	prev_free->next_free = next_free->prev_free = first_hdr;
	first_hdr->next_free = next_free;
	first_hdr->prev_free = prev_free;

	return first_hdr;
}

static struct hdr_base *merge_free_neighbours(void *chunk, void*(*merge)(void*, void*))
{
	struct hdr_base *hdr = chunk;

	if (!hdr->pused)
		hdr = merge(prev_hdr(hdr), hdr);

	struct hdr_base *next = next_hdr(hdr);

	if (!next->cused)
		hdr = merge(hdr, next);
	return hdr;
}

static void append_small_chunk(void *chunk)
{
	struct small_chunk_hdr *hdr = chunk;

	if (!state.small) {
		hdr->next_free = hdr;
		hdr->prev_free = hdr;
		state.small = hdr;
		return;
	}

	hdr->prev_free = state.small->prev_free;
	hdr->next_free = state.small;

	state.small->prev_free->next_free = hdr;
	state.small->prev_free = hdr;
}

static int more_small_chunks(void)
{
	const size_t size = 128 * SMALL_MAX_SIZE;

	void *chunk = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (!chunk) {
		perror("mmap");
		return -1;
	}

	set_small_chunk(chunk, true, false, size - 2 * sizeof(struct hdr_base), state.small, NULL);
	set_hdr(next_hdr(chunk), false, true, 0);
	append_small_chunk(chunk);
}

void *ft_malloc(size_t n)
{
	if (!state.small)
		init_malloc();

	n = ROUND_UP(n, MALLOC_ALIGN);

        struct hdr_base *chunk = NULL;
        if (n <= TINY_MAX_SIZE) {
		chunk = find_best_tiny(n);
		
		if (chunk && chunk->size > n && n - chunk->size > TINY_MIN_SIZE)
			chunk = split_chunk_no_ftr(chunk, n);
	}

	if (!chunk && n <= SMALL_MAX_SIZE) {
		struct small_chunk_hdr *small_chunk = find_best_small(n);


		if (!chunk) {
			more_small_chunks();
			small_chunk = find_best_small(n);
		}

		if (small_chunk && small_chunk->base.size > n && n - small_chunk->base.size > SMALL_MIN_SIZE)
			chunk = &split_small_chunk_unlinked(small_chunk, n)->base;
		else if (small_chunk->next_free == small_chunk)
			state.small = NULL;

		chunk = &small_chunk->base;
	} else if (!chunk) {
		//TODO mmapped alloc
	}

	if (!chunk) {
		errno = ENOMEM;
		return NULL;
	}

	set_used(chunk, true);
	return get_userptr_beg(chunk);
}

void ft_free(void *userptr)
{
	if (!userptr)
		return;

	struct hdr_base *chunk = get_chunkptr(userptr);

	if (is_tiny(chunk)) {
		chunk = merge_free_neighbours(chunk, merge_chunks);
		set_used(chunk, false);
		next_hdr(chunk)->pused = chunk->cused = 0;
	} else if (is_large(chunk)) {
		//TODO munmap
	} else {
		chunk = merge_free_neighbours(chunk, merge_small_chunks);
		set_used(chunk, false);
		//is small
	}
}

static void show_tiny_mem(void)
{
	struct hdr_base *hdr = state.tiny;

	while (hdr->size) {
		void* beg = get_userptr_beg(hdr);
		void* end = get_userptr_end(hdr);

		if (hdr->cused)
			ft_printf("A");
		else
			ft_printf("F");
		ft_printf(" ");

		ft_printf("%p - %p : %zu bytes\n", beg, end, end - beg);
		hdr = next_hdr(hdr);
	}
}

static void show_small_mem(void)
{
	if (!state.small)
		return;

	struct hdr_base *hdr = (struct hdr_base *) state.small;

	do {
		void* beg = get_userptr_beg(hdr);
		void* end = get_userptr_end(hdr);

		if (hdr->cused)
			ft_printf("A");
		else
			ft_printf("F");
		ft_printf(" ");

		ft_printf("%p - %p : %zu bytes\n", beg, end, end - beg);
		hdr = next_hdr(hdr);

	} while (hdr != (struct hdr_base*) state.small);
}

void show_alloc_mem(void)
{
	ft_printf("TINY\n");
	show_tiny_mem();
	//ft_printf("SMALL\n");
	//show_small_mem();
}
