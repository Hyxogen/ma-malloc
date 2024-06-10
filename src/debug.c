#include "ma/internal.h"

#include <errno.h>
#include <signal.h>

#if MA_TRACES
#include <fcntl.h>
#endif

#if FT_BONUS
#include <stdlib.h>
#endif

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
	(void) err;
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

void ma_dump(void) { ma_dump_arena(ma_get_current_arena()); }

#if MA_TRACK_CHUNKS
void ma_debug_add_chunk(struct ma_debug **list, const struct ma_hdr *chunk)
{
	struct ma_debug *cur = *list;

	while (cur) {
		for (size_t i = 0; i < sizeof(cur->_entries)/sizeof(cur->_entries[0]); ++i) {
			if (!cur->_entries[i]) {
				cur->_entries[i] = chunk;
				return;
			}
		}

		if (cur->_next)
			cur = cur->_next;
		else
			break;
	}

	struct ma_debug *new = ma_sysalloc(sizeof(*new));
	if (new == MA_SYSALLOC_FAILED) {
		eprint("ma_sysalloc: failed to allocate memory for debug tracking\n");
		return;
	}

	if (*list) {
		cur->_next = new;
		new->_prev = cur;
	} else {
		*list = new;
	}

	new->_entries[0] = chunk;
}

void ma_debug_rem_chunk(struct ma_debug **list, const struct ma_hdr *chunk)
{
	struct ma_debug *cur = *list;

	while (cur) {
		for (size_t i = 0; i < sizeof(cur->_entries)/sizeof(cur->_entries[0]); ++i) {
			if (cur->_entries[i] == chunk) {
				cur->_entries[i] = NULL;
				return;
			}
		}
		cur = cur->_next;
	}
}

void ma_debug_for_each(const struct ma_debug *list, void (*f)(const struct ma_hdr *))
{
	const struct ma_debug *cur = list;

	while (cur) {
		for (size_t i = 0; i < sizeof(cur->_entries)/sizeof(cur->_entries[0]); ++i) {
			if (cur->_entries[i])
				f(cur->_entries[i]);
		}
		cur = cur->_next;
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
