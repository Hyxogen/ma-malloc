#include "ma/internal.h"

#include <errno.h>
#include <ft/ctype.h>
#include <signal.h>

#if MA_TRACES
#include <fcntl.h>
#endif

#if MA_TRACK_CHUNKS
#include <ft/string.h>
#endif

#if FT_BONUS
#include <stdlib.h>
#endif

struct show_alloc_mem_ctx {
	size_t total;
	bool hexdump;
};

// TODO implement in libft
[[noreturn]] void ft_abort(void)
{
#if FT_BONUS
	abort();
#else
	pthread_kill(pthread_self(), SIGABRT);
	pthread_kill(pthread_self(), SIGKILL);

	__builtin_unreachable();
#endif
}

void ft_assert_impl(int pred, const char *predstr, const char *func,
		    const char *file, int line)
{
	if (!pred) {
		eprint("%s:%i: %s: Assertion '%s' failed.\n", file, line, func,
		       predstr);
		ma_dump();
		ft_abort();
	}
}

char *ft_strerror(int err)
{
#if FT_BONUS
	return strerror(err);
#else
	(void)err;
	return "no error information because of mandatory mode";
#endif
}

void ft_perror(const char *s)
{
#if FT_BONUS
	perror(s);
#else
	if (s)
		eprint("%s", s);
	else
		eprint("mamalloc");
	eprint(": %s\n", ft_strerror(errno));
#endif
}

static void ma_hexdump_to(int fd, const void *p, size_t n, int width)
{
	if (width < 0) {
		width = 1;
		for (size_t i = n; i; i /= 16) {
			width += 1;
		}
	}

	const unsigned char *s = (const unsigned char *)p;
	for (size_t offset = 0; offset < n; offset += 16) {
		if (offset)
			ft_dprintf(fd, "\n");
		ft_dprintf(fd, "%0*zu:", width, offset);

		for (size_t i = 0; i < 16; ++i) {
			if ((i % 2) == 0)
				ft_dprintf(fd, " ");

			if (offset + i >= n)
				ft_dprintf(fd, "  ");
			else
				ft_dprintf(fd, "%02hhx", s[offset + i]);
		}

		ft_dprintf(fd, "  ");

		for (size_t i = 0; i < 16 && offset + i < n; ++i) {
			if (ft_isprint(s[offset + i]))
				ft_dprintf(fd, "%c", s[offset + i]);
			else
				ft_dprintf(fd, ".");
		}
	}
	ft_dprintf(fd, "\n");
}

static void ma_show_alloc_mem_list(const struct ma_hdr *hdr, void *opaque)
{
	if (!hdr)
		return;
	struct show_alloc_mem_ctx *ctx = (struct show_alloc_mem_ctx *)opaque;
	int width = 8;

	switch (ma_get_size_class(hdr)) {
	case MA_SMALL:
		ft_printf("TINY");
		break;
	case MA_LARGE:
		ft_printf("SMALL");
		break;
	case MA_HUGE:
		width = -1;
		ft_printf("LARGE");
	}
	ft_printf(" : 0x%lX\n", (unsigned long)hdr);

	while (!ma_is_sentinel(hdr)) {
		if (ma_is_inuse(hdr)) {
			void *p = ma_chunk_to_mem(hdr);
			size_t size = ma_get_size(hdr);

			ft_printf(
			    "0x%lX - 0x%lX : %zu bytes\n", (unsigned long)p,
			    (unsigned long)((unsigned char *)p + size), size);

			if (ctx->hexdump)
				ma_hexdump_to(STDOUT_FILENO, p, size, width);

			ctx->total += size;
		}
		hdr = ma_next_hdr(hdr);
	}
}

static void ma_show_alloc_mem_no_lock(const struct ma_arena *arena,
				      bool hexdump)
{
	struct show_alloc_mem_ctx ctx = {.total = 0, .hexdump = hexdump};

	ma_debug_for_each(&arena->debug, ma_show_alloc_mem_list, &ctx);

	ft_printf("Total : %zu bytes\n", ctx.total);
}

void ma_show_alloc_mem(bool hexdump)
{
	ma_maybe_initialize();

	struct ma_arena *arena = ma_get_current_arena();

	ma_lock_arena(arena);

	ma_show_alloc_mem_no_lock(arena, hexdump);

	ma_unlock_arena(arena);
}

void show_alloc_mem(void) { ma_show_alloc_mem(false); }
void show_alloc_mem_ex(void) { ma_show_alloc_mem(true); }

void ma_dump(void) { ma_dump_arena(ma_get_current_arena()); }

void ma_init_debug(struct ma_debug *debug)
{
	debug->num_entries = 0;
	debug->capacity = 0;
	debug->entries = NULL;
}

#if MA_TRACK_CHUNKS

void ma_debug_sort(struct ma_debug *debug)
{
	for (size_t i = 1; i < debug->num_entries; ++i) {
		if (!debug->entries[i])
			continue;
		if (debug->entries[i - 1] == NULL ||
		    (debug->entries[i - 1] > debug->entries[i])) {
			const struct ma_hdr *tmp = debug->entries[i - 1];
			debug->entries[i - 1] = debug->entries[i];
			debug->entries[i] = tmp;
			i = 1;
		}
	}
}

void ma_debug_add_chunk(struct ma_debug *debug, const struct ma_hdr *chunk)
{
	if (debug->num_entries == debug->capacity) {
		size_t new_cap =
		    debug->capacity == 0 ? 1024 : debug->capacity * 2;
		const struct ma_hdr **new_entries =
		    ma_sysalloc(new_cap * sizeof(*debug->entries));

		if (new_entries == MA_SYSALLOC_FAILED) {
			eprint(
			    "ma_sysalloc: failed to allocate memory for debug tracking");
			return;
		}

		if (debug->capacity > 0) {
			ft_memcpy(new_entries, debug->entries,
				  sizeof(*debug->entries) * debug->num_entries);
			ma_sysfree(debug->entries, sizeof(*debug->entries) *
						       debug->num_entries);
		}

		debug->capacity = new_cap;
		debug->entries = new_entries;
	}

	debug->entries[debug->num_entries++] = chunk;
	ma_debug_sort(debug);
}

void ma_debug_rem_chunk(struct ma_debug *debug, const struct ma_hdr *chunk)
{
	for (size_t i = 0; i < debug->num_entries; ++i) {
		if (debug->entries[i] == chunk) {
			debug->entries[i] = NULL;
			break;
		}
	}
	ma_debug_sort(debug);
	debug->num_entries -= 1;
}

void ma_debug_for_each(const struct ma_debug *debug,
		       void (*f)(const struct ma_hdr *, void *), void *ctx)
{
	for (size_t i = 0; i < debug->num_entries; ++i) {
		f(debug->entries[i], ctx);
	}
}
#endif

#if MA_TRACES
int ma_dump_fd = -1;

char ma_prog_name[512];

static void ma_get_prog_name(void)
{
	int fd = open("/proc/self/cmdline", O_RDONLY);
	if (fd < 0) {
		ft_perror("open");
		abort();
	}

	read(fd, ma_prog_name, sizeof(ma_prog_name));

	close(fd);
}

void ma_maybe_init_dump(void)
{
	if (ma_dump_fd >= 0)
		return;

	ma_get_prog_name();

	char dump_name[512];
	ft_snprintf(dump_name, sizeof(dump_name), "%s-dump.txt", ma_prog_name);

	ma_dump_fd = open(dump_name, O_CREAT | O_TRUNC | O_WRONLY, 0777);
	if (ma_dump_fd < 0) {
		ft_perror("open");
		abort();
	}
}

__attribute__((destructor)) static void ma_close_dump(void)
{
	if (ma_dump_fd < 0)
		return;

	if (close(ma_dump_fd))
		ft_perror("close");
}

#endif
