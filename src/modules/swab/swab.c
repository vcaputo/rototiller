/*
 *  Copyright (C) 2019 - Vito Caputo - <vcaputo@pengaru.com>
 *
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
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

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_util.h"

#include "din/din.h"


typedef struct swab_context_t {
	til_module_context_t	til_module_context;
	din_t			*din;
	float			r;
} swab_context_t;

typedef struct color_t {
	float			r,g,b;
} color_t;

typedef struct v3f_t {
	float			x, y, z;
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


static til_module_context_t * swab_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	swab_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(swab_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->din = din_new(12, 12, 100, seed);
	if (!ctxt->din)
		return til_module_context_free(&ctxt->til_module_context);

	return &ctxt->til_module_context;
}


static void swab_destroy_context(til_module_context_t *context)
{
	swab_context_t	*ctxt = (swab_context_t *)context;

	din_free(ctxt->din);
	free(ctxt);
}


static void swab_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	swab_context_t	*ctxt = (swab_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_tile64 };

	ctxt->r += .0001f;
}


static void swab_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	swab_context_t		*ctxt = (swab_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;
	unsigned		frag_w = fragment->width, frag_h = fragment->height;

	float	cos_r = cos(ctxt->r);
	float	sin_r = sin(ctxt->r);
	float	z1 = cos_r;
	float	z2 = sin_r;
	float	xscale = 1.f / (float)fragment->frame_width;
	float	yscale = 1.f / (float)fragment->frame_height;
	float	yscaled;
	v3f_t	t_coord = {
			.z = -z2
		};
	v3f_t	r_coord = {
			.z = z1
		};
	v3f_t	g_coord = {
			.z = -z1
		};
	v3f_t	b_coord = {
			.z = z2
		};

	yscaled = (float)fragment->y * yscale;
	for (unsigned y = 0; y < frag_h; y++, yscaled += yscale) {
		float	xscaled = (float)fragment->x * xscale;

		t_coord.y = yscaled * .5f;
		r_coord.y = yscaled * .7f;
		g_coord.y = yscaled * .93f;
		b_coord.y = yscaled * .81f;

		for (unsigned x = 0; x < frag_w; x++, xscaled += xscale) {
			color_t		color;
			uint32_t	pixel;
			float		t;

			t_coord.x = xscaled * .5f;
			r_coord.x = xscaled * .7f;
			g_coord.x = xscaled * .93f;
			b_coord.x = xscaled * .81f;

			t = din(ctxt->din, &t_coord) * 33.f;
			color.r = din(ctxt->din, &r_coord) * t;
			color.g = din(ctxt->din, &g_coord) * t;
			color.b = din(ctxt->din, &b_coord) * t;

			pixel = color_to_uint32(color);
			til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, pixel);
		}
	}
}


til_module_t	swab_module = {
	.create_context = swab_create_context,
	.destroy_context = swab_destroy_context,
	.prepare_frame = swab_prepare_frame,
	.render_fragment = swab_render_fragment,
	.name = "swab",
	.description = "Colorful perlin-noise visualization (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
