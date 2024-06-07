#include "ma/internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if MA_TRACES
#include <fcntl.h>
#endif

// TODO implement in libft
void ft_abort(void) { abort(); }

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

char *ft_strerror(int err) { return strerror(err); }

void ft_perror(const char *s) { perror(s); }

void ma_dump(void) { ma_dump_arena(ma_get_current_arena()); }

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
