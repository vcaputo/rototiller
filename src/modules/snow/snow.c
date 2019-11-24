#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "fb.h"
#include "rototiller.h"

/* Copyright (C) 2019 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements white noise / snow just using rand() */

/* To avoid the contention around rand() I'm just using rand_r()
 * in an entirely racy fashion with a single seed from the threaded
 * render_fragment().  It should really be per-cpu but the module
 * api doesn't currently send down a cpu identifier to the render
 * function, so TODO in the future add that.
 */
static int	snow_seed;

static int snow_fragmenter(void *context, const fb_fragment_t *fragment, unsigned number, fb_fragment_t *res_fragment)
{
	return fb_fragment_slice_single(fragment, 32, number, res_fragment);
}


static void snow_prepare_frame(void *context, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter)
{
	*res_fragmenter = snow_fragmenter;
}


static void snow_render_fragment(void *context, unsigned cpu, fb_fragment_t *fragment)
{
	for (unsigned y = fragment->y; y < fragment->y + fragment->height; y++) {
		for (unsigned x = fragment->x; x < fragment->x + fragment->width; x++) {
			uint32_t	pixel = rand_r(&snow_seed) % 256;

			fb_fragment_put_pixel_unchecked(fragment, x, y, pixel << 16 | pixel << 8 | pixel);
		}
	}
}


rototiller_module_t	snow_module = {
	.prepare_frame = snow_prepare_frame,
	.render_fragment = snow_render_fragment,
	.name = "snow",
	.description = "TV snow / white noise",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
