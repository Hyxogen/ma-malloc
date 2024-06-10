#include "ma/internal.h"

#include <errno.h>
#include <ft/stdlib.h>

// At the moment, the get_current_arena and get_arena functions don't do
// anything special, these are just placeholder functions for when I want to add
// proper multithreading support

static struct ma_state state;

struct ma_arena *ma_get_current_arena(void) { return &state.main_arena; }

struct ma_arena *ma_get_arena(const void *p)
{
	(void)p;
	return &state.main_arena;
}

static void ma_init_opts(struct ma_opts *opts)
{
	opts->perturb = false;

	char *val;
	if ((val = ft_getenv("MALLOC_PERTURB_"))) {
		char *end;
		unsigned long long perturb = ft_strtoull(val, &end, 0);

		if (perturb != ULLONG_MAX && errno != ERANGE) {
			opts->perturb = true;
			opts->perturb_byte = ~(uint8_t)perturb;
		} else {
			eprint("MALLOC_PERTURB_: %s: invalid value\n", val);
		}
	}
}

const struct ma_opts *ma_get_opts(void) { return &state.opts; }

void ma_maybe_initialize(void)
{
	if (!state.initialized) {
		state.initialized = true;

		// we call ma_sysalloc_granularity here to load the pagesize and
		// avoid races when we actually need it during allocation
		ma_sysalloc_granularity();

		ma_init_opts(&state.opts);
		ma_init_arena(&state.main_arena);
	}
}
