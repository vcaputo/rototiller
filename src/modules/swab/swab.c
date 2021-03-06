/*
 *  Copyright (C) 2019 - Vito Caputo - <vcaputo@pengaru.com>
 *
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 3 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "din/din.h"

#include "fb.h"
#include "rototiller.h"
#include "util.h"


typedef struct swab_context_t {
	din_t		*din;
	float		r;
	unsigned	n_cpus;
} swab_context_t;

typedef struct color_t {
	float	r,g,b;
} color_t;

typedef struct v3f_t {
	float	x, y, z;
} v3f_t;


/* convert a color into a packed, 32-bit rgb pixel value (taken from libs/ray/ray_color.h) */
static inline uint32_t color_to_uint32(color_t color) {
	uint32_t	pixel;

	if (color.r > 1.0f) color.r = 1.0f;
	if (color.g > 1.0f) color.g = 1.0f;
	if (color.b > 1.0f) color.b = 1.0f;

	if (color.r < .0f) color.r = .0f;
	if (color.g < .0f) color.g = .0f;
	if (color.b < .0f) color.b = .0f;

	pixel = (uint32_t)(color.r * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.g * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.b * 255.0f);

	return pixel;
}


static void * swab_create_context(unsigned ticks, unsigned num_cpus)
{
	swab_context_t	*ctxt;

	ctxt = calloc(1, sizeof(swab_context_t));
	if (!ctxt)
		return NULL;

	ctxt->din = din_new(12, 12, 100);
	if (!ctxt->din) {
		free(ctxt);
		return NULL;
	}

	return ctxt;
}


static void swab_destroy_context(void *context)
{
	swab_context_t	*ctxt = context;

	din_free(ctxt->din);
	free(ctxt);
}


static int swab_fragmenter(void *context, const fb_fragment_t *fragment, unsigned number, fb_fragment_t *res_fragment)
{
	swab_context_t	*ctxt = context;

	return fb_fragment_slice_single(fragment, ctxt->n_cpus, number, res_fragment);
}


static void swab_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter)
{
	swab_context_t	*ctxt = context;

	*res_fragmenter = swab_fragmenter;

	ctxt->r += .0001f;
	ctxt->n_cpus = n_cpus;
}


static void swab_render_fragment(void *context, unsigned ticks, unsigned cpu, fb_fragment_t *fragment)
{
	swab_context_t	*ctxt = context;
	float		cos_r = cos(ctxt->r);
	float		sin_r = sin(ctxt->r);
	float		z1 = cos_r;
	float		z2 = sin_r;
	float		xscale = 1.f / (float)fragment->frame_width;
	float		yscale = 1.f / (float)fragment->frame_height;

	for (int y = fragment->y; y < fragment->y + fragment->height; y++) {
		for (int x = fragment->x; x < fragment->x + fragment->width; x++) {
			color_t		color;
			uint32_t	pixel;
			float		t;

			t = din(ctxt->din, &(v3f_t){ .x = (float)x * xscale * .5f, .y = (float)y * yscale * .5f, .z = -z2 }) * 33.f;

			color.r = din(ctxt->din, &(v3f_t){ .x = (float)x * xscale * .7f, .y = (float)y * yscale * .7f, .z = z1 }) * t;
			color.g = din(ctxt->din, &(v3f_t){ .x = (float)x * xscale * .93f, .y = (float)y * yscale * .93f, .z = -z1 }) * t;
			color.b = din(ctxt->din, &(v3f_t){ .x = (float)x * xscale * .81f, .y = (float)y * yscale * .81f, .z = z2 }) * t;

			pixel = color_to_uint32(color);
			fb_fragment_put_pixel_unchecked(fragment, x, y, pixel);
		}
	}
}


rototiller_module_t	swab_module = {
	.create_context = swab_create_context,
	.destroy_context = swab_destroy_context,
	.prepare_frame = swab_prepare_frame,
	.render_fragment = swab_render_fragment,
	.name = "swab",
	.description = "Colorful perlin-noise visualization (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv3",
};
