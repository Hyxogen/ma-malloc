#include "malloc.h"

#include <assert.h>
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

#define ROUND_UP(x, boundary) ((x + boundary - 1) & ~(boundary - 1))
#define IS_ALGINED_TO(x, boundary) ((x & (boundary - 1)) == 0)

struct hdr_base {
	size_t pused : 1;
	size_t cused : 1;
	size_t size : 62;
};

struct chunk_ftr {
	size_t size;
};

static struct malloc_state {
	void *mem;
} state;

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
	assert(!hdr->cused && "no footer present in an empty chunk");
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

static struct hdr_base *next_hdr(const void *addr)
{
	const struct hdr_base *hdr = addr;
	return (struct hdr_base *) ((char*) addr + sizeof(struct hdr_base) + hdr->size);
}

static struct hdr_base *prev_hdr(const void *addr)
{
	const struct hdr_base *hdr = addr;
	assert(hdr->pused == 0 && "cannot determine previous header when in use");

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

static void init_malloc(void)
{
	int page_size = getpagesize();
	state.mem = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	
	if (state.mem == MAP_FAILED) {
		perror("mmap");
		abort();
	}

	set_chunk(state.mem, true, false, page_size - 2 * sizeof(struct hdr_base));
	set_hdr(next_hdr(state.mem), false, true, 0);
}

static struct hdr_base* find_best(size_t n)
{
	struct hdr_base *cur = state.mem;
	struct hdr_base *best = NULL;

	while (cur->size != 0) {
		if (!best)
			best = cur;
		else if (cur->size == n)
			return cur;
		else if (cur->size > n && cur->size < best->size)
			best = cur;
		cur = next_hdr(best);
	}
	return best;
}

static struct hdr_base* split_chunk_no_ftr(void *chunk, size_t new_size)
{
	struct hdr_base *hdr = chunk;

	assert(hdr->size > new_size);
	assert(new_size >= MALLOC_ALIGN);
	size_t rem = hdr->size - new_size - sizeof(struct hdr_base);

	assert(rem > sizeof(struct hdr_base) + sizeof(struct chunk_ftr));
	assert(IS_ALGINED_TO(rem, MALLOC_ALIGN));

	hdr->size = new_size;

	set_chunk(next_hdr(hdr), false, false, rem);
	return hdr;
}

static void mark_used(void *chunk)
{
	struct hdr_base *hdr = chunk;

	hdr->cused = 1;
	next_hdr(hdr)->pused = 1;
}

//first should be lower addressed
static struct hdr_base *merge_chunks(void *first, void *second)
{
	struct hdr_base *first_hdr = first, *second_hdr = second;

	size_t new_size = first_hdr->size + second_hdr->size + sizeof(struct hdr_base);
	set_chunk(first_hdr, true, false, new_size);

	return first_hdr;
}

static struct hdr_base *merge_free_neighbours(void *chunk)
{
	struct hdr_base *hdr = chunk;

	if (!hdr->pused)
		hdr = merge_chunks(prev_hdr(hdr), hdr);

	struct hdr_base *next = next_hdr(hdr);

	if (!next->cused)
		hdr = merge_chunks(hdr, next);
}

void *ft_malloc(size_t n)
{
	if (!state.mem)
		init_malloc();

	n = ROUND_UP(n, MALLOC_ALIGN);
	struct hdr_base *chunk = find_best(n);

	if (!chunk) { //TODO try to allocate more vmem
		errno = ENOMEM;
		return NULL;
	}

	if (chunk->size > n && n - chunk->size > sizeof(struct hdr_base) + sizeof(struct chunk_ftr))
		chunk = split_chunk_no_ftr(chunk, n);
	mark_used(chunk);

	return get_userptr_beg(chunk);
}

void ft_free(void *userptr)
{
	struct hdr_base *chunk = get_chunkptr(userptr);

	if (!chunk->cused) {
		ft_dprintf(STDERR_FILENO, "free(): attempt to free non-allocated memory\n");
		abort();
	}

	chunk = merge_free_neighbours(chunk);
	next_hdr(chunk)->pused = chunk->cused = 0;
}

void show_alloc_mem(void)
{
	struct hdr_base *hdr = state.mem;

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
