#include "ma/internal.h"

#include <errno.h>
#include <ma/libc/stdlib.h>

static struct ma_opts *ma_get_opts_mut(void)
{
	static struct ma_opts opts;
	return &opts;
}

const struct ma_opts *ma_get_opts(void) { return ma_get_opts_mut(); }

void ma_init_opts(void)
{
	struct ma_opts *opts = ma_get_opts_mut();
	opts->perturb = false;

	char *val;
	if ((val = ma_getenv("MALLOC_PERTURB_"))) {
		char *end;
		unsigned long long perturb = ma_strtoull(val, &end, 0);

		if (perturb != ULLONG_MAX && errno != ERANGE) {
			opts->perturb = true;
			opts->perturb_byte = ~(uint8_t)perturb;
		} else {
			eprint("MALLOC_PERTURB_: %s: invalid value\n", val);
		}
	}
}
