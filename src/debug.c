#include "ma/internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if MA_TRACES
#include <fcntl.h>
#endif

// TODO implement in libft
void ft_abort(void)
{
	abort();
}

void ft_assert_impl(int pred, const char *predstr, const char *func, const char *file, int line)
{
	if (!pred) {
		eprint("%s:%i: %s: Assertion '%s' failed.\n", file, line, func,
		       predstr);
		ma_dump();
		ft_abort();
	}
}

char *ft_strerr(int err)
{
	return strerror(err);
}

void ft_perror(const char *s)
{
	perror(s);
}

void ma_dump(void)
{
	ma_dump_arena(ma_get_current_arena());
}

#if MA_TRACES
int dump_fd = -1;

void ma_maybe_init_dump(void)
{
	if (dump_fd >= 0)
		return;

	dump_fd = open("dump.txt", O_CREAT | O_TRUNC | O_WRONLY, 0777);
	if (dump_fd < 0) {
		ft_perror("open");
		abort();
	}
}

__attribute__ ((destructor)) static void ma_close_dump(void)
{
	if (dump_fd < 0)
		return;

	if (close(dump_fd))
		ft_perror("close");
}

#endif
