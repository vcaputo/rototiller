#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

/* Copyright (C) 2019 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements white noise / snow just using rand() */

typedef union snow_seed_t {
	char			__padding[256];		/* prevent seeds sharing a cache-line */
	unsigned		seed;
} snow_seed_t;

typedef struct snow_context_t {
	til_module_context_t	til_module_context;
	unsigned		unused;
	snow_seed_t		seeds[];
} snow_context_t;


static til_module_context_t * snow_create_context(unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	snow_context_t	*ctxt;

	ctxt = til_module_context_new(sizeof(snow_context_t) + n_cpus * sizeof(snow_seed_t), seed, ticks, n_cpus, path);
	if (!ctxt)
		return NULL;

	for (unsigned i = 0; i < n_cpus; i++)
		ctxt->seeds[i].seed = rand_r(&seed);

	return &ctxt->til_module_context;
}


static void snow_prepare_frame(til_module_context_t *context, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu };
}


static void snow_render_fragment(til_module_context_t *context, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	snow_context_t		*ctxt = (snow_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	unsigned	*seed = &ctxt->seeds[cpu].seed;

	for (unsigned y = fragment->y; y < fragment->y + fragment->height; y++) {
		for (unsigned x = fragment->x; x < fragment->x + fragment->width; x++) {
#ifdef __WIN32__
			uint32_t	pixel = rand();
#else
			uint32_t	pixel = rand_r(seed) % 256;
#endif

			til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, pixel << 16 | pixel << 8 | pixel);
		}
	}
}


til_module_t	snow_module = {
	.create_context = snow_create_context,
	.prepare_frame = snow_prepare_frame,
	.render_fragment = snow_render_fragment,
	.name = "snow",
	.description = "TV snow / white noise (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
