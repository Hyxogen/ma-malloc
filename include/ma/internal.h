#ifndef FT_INTERNAL_H
#define FT_INTERNAL_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ma/libc/stdio.h>
#include <ma/threads.h>
#include <unistd.h>
/*
 * The subject for which this malloc was made requires that the implemention has
 * different "zones" for a size of allocation. Thus small allocation may not be
 * made out of large zones and the reverse. The allocator strategy is therefor a
 * seggregated bestfit.
 *
 * This can be switched to a total bestfit stragegy by setting
 * MA_SEGREGATED_BESTFIT to 1
 */
#ifndef MA_CHECK_SELF
#define MA_CHECK_SELF 0
#endif

#ifndef MA_SEGREGATED_BESTFIT
#define MA_SEGREGATED_BESTFIT 0
#endif

#ifndef MA_GLIBC_COMPATIBLE
#define MA_GLIBC_COMPATIBLE 1
#endif

#ifndef MA_COMPILE_AS_LIBC
#define MA_COMPILE_AS_LIBC 1
#endif

#ifndef MA_TRACES
#define MA_TRACES 0
#endif

#ifndef MA_TRACK_CHUNKS
#define MA_TRACK_CHUNKS 0
#endif

#ifndef FT_BONUS
#define FT_BONUS 0
#endif

#define MA_PINUSE_FLAG ((size_t)0b001)
#define MA_SMALL_FLAG ((size_t)0b010)
#define MA_LARGE_FLAG ((size_t)0b100)
#define MA_FLAGS_MASK (MA_PINUSE_FLAG | MA_LARGE_FLAG | MA_SMALL_FLAG)
#define MA_SIZE_MASK (~MA_FLAGS_MASK)

#define MA_HEADER_SIZE (sizeof(uint64_t))
#define MA_FOOTER_SIZE (sizeof(uint64_t))

#define MA_MALLOC_ALIGN (_Alignof(max_align_t))
#define MA_HALF_MALLOC_ALIGN (MA_MALLOC_ALIGN / 2)

#define MA_MIN_CHUNK_SIZE (4 * MA_HEADER_SIZE)
#define MA_MIN_ALLOC_SIZE (MA_MIN_CHUNK_SIZE - MA_HEADER_SIZE)

#define MA_SMALLBIN_COUNT 64
#define MA_LARGEBIN_COUNT 64
#define MA_BINMAPS_PER_ENTRY 64
#define MA_SMALLBIN_STEP (MA_MALLOC_ALIGN)
#define MA_BIN_COUNT (MA_SMALLBIN_COUNT + MA_LARGEBIN_COUNT)

#define MA_MIN_SMALL_SIZE (MA_MIN_ALLOC_SIZE)
#define MA_MAX_SMALL_SIZE                                                      \
	((MA_MIN_SMALL_SIZE + MA_SMALLBIN_STEP * (MA_SMALLBIN_COUNT - 1)) -    \
	 MA_HEADER_SIZE)

#define MA_MIN_LARGE_SIZE (MA_MAX_SMALL_SIZE + MA_HEADER_SIZE)
#define MA_MAX_LARGE_SIZE (1024 * 1024 - MA_HEADER_SIZE)

#define MA_CHUNKS_PER_ZONE 128

#define MA_CHUNK_ALLOC_PADDING (2 * MA_HEADER_SIZE + MA_HALF_MALLOC_ALIGN)

#define MA_SYSALLOC_FAILED ((void *)-1)

_Static_assert(2 * MA_HEADER_SIZE == MA_MALLOC_ALIGN,
	       "basic assumption for alignment implementation");
_Static_assert(MA_HEADER_SIZE == MA_HALF_MALLOC_ALIGN,
	       "basic assumption for alignment implementation");
/*
 * malloc requires that returned pointers are aligned to at least
 * _Alignof(max_align_t). The implementation of this malloc achieves that by
 * using the assumption that the header size is half of the alignment
 * requirement. Having the implementation like this:
 *
 * Headers always start with MA_HEADER_SIZE in the lower address, for
 * _Alignof(max_align_t) == 16 that would be 0x8 (half of the alignment)
 *
 * as the header size is half of the alignment, the user pointer (the pointer
 * returned from a call to malloc) would always be properly aligned.
 *
 * all memory allocations make sure that the next header will start at
 * MA_HALF_MALLOC_ALIGN
 */

#ifdef FT_NDEBUG
#define ft_assert(pred)
#else
#define ft_assert(pred)                                                        \
	ft_assert_impl(!(!(pred)), #pred, __FUNCTION__, __FILE__, __LINE__)
#endif

// TODO rename to ma_eprint
#define eprint(...) ma_dprintf(STDERR_FILENO, __VA_ARGS__)

#define MA_ALIGN_UP(x, boundary) (((x) + (boundary)-1) & ~((boundary)-1))
#define MA_ALIGN_DOWN(x, boundary) (((uintptr_t)(x)) & ~((boundary)-1))
#define MA_IS_ALIGNED_TO(x, boundary) (((uintptr_t)(x) & ((boundary)-1)) == 0)
#define MA_IS_MULTIPLE_OF(x, m) ((((x) / (m)) * (m)) == (x))
#define MA_IS_POWER_OF_TWO_OR_ZERO(x) (((x) & ((x)-1)) == 0)

#if MA_TRACES
extern int ma_dump_fd;

void ma_maybe_init_dump(void);

#define ma_dump_print(...)                                                     \
	do {                                                                   \
		ma_maybe_init_dump();                                          \
		ma_dprintf(ma_dump_fd, __VA_ARGS__);                           \
	} while (0)

#else
#define ma_dump_print(...)
#endif

enum ma_size_class {
	MA_SMALL,
	MA_LARGE,
	MA_HUGE,
};

struct ma_hdr {
	uint64_t tag;

	struct ma_hdr *next;
	struct ma_hdr *prev;
};

struct ma_debug {
	size_t num_entries;
	size_t capacity;
	const struct ma_hdr **entries;
};

struct ma_arena {
	struct ma_hdr *tops[2];
	struct ma_hdr *bins[MA_BIN_COUNT];
	uint64_t bin_maps[2];

	struct ma_debug debug;

	ma_mtx mtx;
};
_Static_assert(MA_BIN_COUNT <= sizeof(uint64_t) * 2 * CHAR_BIT,
	       "required to use fast bin bitmaps");

struct ma_opts {
	bool perturb;
	uint8_t perturb_byte;
};

struct ma_state {
	struct ma_arena main_arena;
	struct ma_opts opts;
	bool initialized;
};

void *ma_malloc(size_t n);
void *ma_calloc(size_t nmemb, size_t size);
void *ma_realloc(void *p, size_t newsize);
size_t ma_malloc_usable_size(void *p);
void *ma_aligned_alloc(size_t align, size_t n);
void *ma_memalign(size_t align, size_t size);
int ma_posix_memalign(void **memptr, size_t align, size_t size);
void *ma_valloc(size_t size);
void *ma_pvalloc(size_t size);
void ma_free(void *p);

void *ma_malloc_no_lock(struct ma_arena *arena, size_t n);
void ma_free_no_lock(struct ma_arena *arena, void *p);

bool ma_is_inuse(const struct ma_hdr *hdr);
bool ma_is_pinuse(const struct ma_hdr *chunk);
void ma_set_size(struct ma_hdr *chunk, size_t newsize);
size_t ma_get_size(const void *tag);
bool ma_is_small(const struct ma_hdr *chunk);
bool ma_is_large(const struct ma_hdr *chunk);
bool ma_is_huge(const void *chunk);
bool ma_is_binable(const struct ma_hdr *chunk);
void *ma_chunk_to_mem(const struct ma_hdr *chunk);
struct ma_hdr *ma_mem_to_chunk(const void *p);
struct ma_hdr *ma_next_hdr(const void *chunk);
struct ma_hdr *ma_prev_hdr(const void *chunk);
bool ma_is_sentinel(const struct ma_hdr *chunk);
enum ma_size_class ma_get_size_class(const struct ma_hdr *hdr);
void ma_chunk_fill(struct ma_hdr *chunk, uint8_t byte);

void ma_check_pointer(void *p);

uint64_t ma_ctlz(uint64_t n);
void ma_maybe_initialize(void);
size_t ma_pad_requestsize(size_t size);
enum ma_size_class ma_get_size_class_from_size(size_t size);
void ma_maybe_perturb_alloc(void *p);
void ma_maybe_perturb_free(void *p);

// returns false if an allocation should return NULL, might set errno
bool ma_check_requestsize(size_t n);
size_t ma_get_prealloc_size(enum ma_size_class class);

// returns false on a failure
void ma_lock_arena(struct ma_arena *arena);
void ma_unlock_arena(struct ma_arena *arena);

void ma_maybe_split(struct ma_arena *arena, struct ma_hdr *chunk,
		    size_t alloc_size);
bool ma_should_split(const struct ma_hdr *chunk, size_t alloc_size);
struct ma_hdr *ma_split_chunk(struct ma_hdr *chunk, size_t newsize);
struct ma_hdr *ma_merge_chunks(struct ma_hdr *a, struct ma_hdr *b);
struct ma_hdr *ma_find_bestfit(const struct ma_hdr *list, size_t size);
struct ma_hdr *ma_init_chunk(struct ma_hdr *chunk, enum ma_size_class class,
			     size_t size, bool pinuse);
void ma_make_sentinel(struct ma_hdr *chunk, enum ma_size_class class,
		      bool pinuse);
void ma_set_inuse(struct ma_hdr *chunk, bool v);
void ma_set_pinuse(struct ma_hdr *chunk, bool v);
void ma_unlink_chunk(struct ma_hdr **list, struct ma_hdr *chunk);
void ma_append_chunk(struct ma_hdr **list, struct ma_hdr *chunk);

struct ma_hdr *ma_alloc_chunk(struct ma_arena *arena, size_t minsize,
			      enum ma_size_class class);
void ma_dealloc_chunk(struct ma_arena *arena, struct ma_hdr *chunk);

struct ma_hdr *ma_find_in_bins(struct ma_arena *arena, size_t n,
			       struct ma_hdr ***from);

void ma_check_user_chunk(const struct ma_hdr *chunk);

void ma_append_chunk_any(struct ma_arena *arena, struct ma_hdr *chunk);
void ma_unlink_chunk_any(struct ma_arena *arena, struct ma_hdr *chunk);
size_t ma_binidx(size_t size);
size_t ma_freelist_idx_from_size(size_t size);
size_t ma_freelist_idx(const struct ma_hdr *hdr);

void ma_init_arena(struct ma_arena *arena);
struct ma_arena *ma_get_current_arena(void);
struct ma_arena *ma_get_arena(const void *p);
void ma_init_opts(void);
const struct ma_opts *ma_get_opts(void);

void ma_dump_all_chunks(const struct ma_hdr *list, void *unused);
void ma_dump_arena(const struct ma_arena *arena);
void ma_dump(void);

#if defined(FT_NDEBUG) || !MA_CHECK_SELF
inline void ma_assert_correct_all_chunks(const struct ma_hdr *list,
					 void *unused)
{
	(void)list;
	(void)unused;
}
inline void ma_assert_correct_arena(const struct ma_arena *arena)
{
	(void)arena;
}
#else
void ma_assert_correct_all_chunks(const struct ma_hdr *list, void *unused);
void ma_assert_correct_arena(const struct ma_arena *arena);
#endif

void ft_assert_impl(int pred, const char *predstr, const char *func,
		    const char *file, int line);
char *ft_strerror(int err);
void ma_perror(const char *s);

int ma_sysalloc_granularity(void);
void *ma_sysalloc(size_t size);
bool ma_sysfree(void *p, size_t size);

void ma_init_debug(struct ma_debug *debug);
#if MA_TRACK_CHUNKS
void ma_debug_add_chunk(struct ma_debug *debug, const struct ma_hdr *chunk);
void ma_debug_rem_chunk(struct ma_debug *debug, const struct ma_hdr *chunk);
void ma_debug_for_each(const struct ma_debug *list,
		       void (*f)(const struct ma_hdr *, void *), void *ctx);
#else
inline void ma_debug_add_chunk(struct ma_debug *list,
			       const struct ma_hdr *chunk)
{
	(void)list;
	(void)chunk;
}

inline void ma_debug_rem_chunk(struct ma_debug *list,
			       const struct ma_hdr *chunk)
{
	(void)list;
	(void)chunk;
}

inline void ma_debug_for_each(const struct ma_debug *list,
			      void (*f)(const struct ma_hdr *, void *),
			      void *ctx)
{
	(void)list;
	(void)f;
	(void)ctx;
}
#endif
#endif
