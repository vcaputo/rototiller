#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"

/* Copyright (C) 2019 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements white noise / snow just using rand() */

typedef union snow_seed_t {
	char	__padding[256];		/* prevent seeds sharing a cache-line */
	int	seed;
} snow_seed_t;

typedef struct snow_context_t {
	unsigned	n_cpus;
	snow_seed_t	seeds[];
} snow_context_t;


static void * snow_create_context(unsigned ticks, unsigned n_cpus)
{
	snow_context_t	*ctxt;

	ctxt = calloc(1, sizeof(snow_context_t) + n_cpus * sizeof(snow_seed_t));
	if (!ctxt)
		return NULL;

	for (unsigned i = 0; i < n_cpus; i++)
		ctxt->seeds[i].seed = rand();

	ctxt->n_cpus = n_cpus;

	return ctxt;
}


static void snow_destroy_context(void *context)
{
	free(context);
}


static int snow_fragmenter(void *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	snow_context_t	*ctxt = context;

	return til_fb_fragment_slice_single(fragment, ctxt->n_cpus, number, res_fragment);
}


static void snow_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter)
{
	*res_fragmenter = snow_fragmenter;
}


static void snow_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	snow_context_t	*ctxt = context;
	int		*seed = &ctxt->seeds[cpu].seed;

	for (unsigned y = fragment->y; y < fragment->y + fragment->height; y++) {
		for (unsigned x = fragment->x; x < fragment->x + fragment->width; x++) {
#ifdef __WIN32__
			uint32_t	pixel = rand();
#else
			uint32_t	pixel = rand_r(seed) % 256;
#endif

			til_fb_fragment_put_pixel_unchecked(fragment, x, y, pixel << 16 | pixel << 8 | pixel);
		}
	}
}


til_module_t	snow_module = {
	.create_context = snow_create_context,
	.destroy_context = snow_destroy_context,
	.prepare_frame = snow_prepare_frame,
	.render_fragment = snow_render_fragment,
	.name = "snow",
	.description = "TV snow / white noise (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
