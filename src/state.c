#include "ma/internal.h"

// At the moment, the get_current_arena and get_arena functions don't do
// anything special, these are just placeholder functions for when I want to add
// proper multithreading support

static struct ma_state state;

struct ma_arena *ma_get_current_arena(void)
{
	return &state.main_arena;
}

struct ma_arena *ma_get_arena(const void *p)
{
	(void) p;
	return &state.main_arena;
}

void ma_maybe_initialize(void)
{
	if (!state.initialized) {
		state.initialized = true;

		// we call ma_sysalloc_granularity here to load the pagesize and
		// avoid races when we actually need it during allocation
		ma_sysalloc_granularity();
		ma_init_arena(&state.main_arena);
	}
}
