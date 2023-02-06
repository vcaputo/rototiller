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

/* https://en.wikipedia.org/wiki/Metaballs */

#include <stdlib.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_stream.h"
#include "til_tap.h"

#include "din/din.h"

#include "v2f.h"
#include "v3f.h"

#define	META2D_NUM_BALLS	10

typedef struct meta2d_ball_t {
	v2f_t			position;
	float			radius;
	v3f_t			color;
} meta2d_ball_t;

typedef struct meta2d_context_t {
	til_module_context_t	til_module_context;

	struct {
		til_tap_t	min_t, max_t;
	} taps;

	struct {
		float		min_t, max_t;
	} vars;

	float			*min_t, *max_t;

	unsigned		n;
	din_t			*din_a, *din_b;
	float			din_t;
	meta2d_ball_t		balls[META2D_NUM_BALLS];
} meta2d_context_t;


/* convert a color into a packed, 32-bit rgb pixel value (taken from libs/ray/ray_color.h) */
static inline uint32_t color_to_uint32(v3f_t color) {
	uint32_t	pixel;

	if (color.x > 1.0f) color.x = 1.0f;
	if (color.y > 1.0f) color.y = 1.0f;
	if (color.z > 1.0f) color.z = 1.0f;

	if (color.x < .0f) color.x = .0f;
	if (color.y < .0f) color.y = .0f;
	if (color.z < .0f) color.z = .0f;

	pixel = (uint32_t)(color.x * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.y * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.z * 255.0f);

	return pixel;
}


static til_module_context_t * meta2d_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	meta2d_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(meta2d_context_t), stream, seed, ticks, n_cpus, path);
	if (!ctxt)
		return NULL;

	/* perlin noise is used for some organic-ish random movement of the balls */
	ctxt->din_a = din_new(10, 10, META2D_NUM_BALLS + 2, seed);
	ctxt->din_b = din_new(10, 10, META2D_NUM_BALLS + 2, seed);

	for (int i = 0; i < META2D_NUM_BALLS; i++) {
		meta2d_ball_t	*ball = &ctxt->balls[i];

		v2f_rand(&ball->position, &ctxt->til_module_context.seed, &(v2f_t){-.7f, -.7f}, &(v2f_t){.7f, .7f});
		ball->radius = rand_r(&ctxt->til_module_context.seed) / (float)RAND_MAX * .2f + .05f;
		v3f_rand(&ball->color, &ctxt->til_module_context.seed, &(v3f_t){0.f, 0.f, 0.f}, &(v3f_t){1.f, 1.f, 1.f});
	}

	ctxt->taps.min_t = til_tap_init_float(ctxt, &ctxt->min_t, 1, &ctxt->vars.min_t, "min_t");
	ctxt->taps.max_t = til_tap_init_float(ctxt, &ctxt->max_t, 1, &ctxt->vars.max_t, "max_t");

	return &ctxt->til_module_context;
}


static void meta2d_destroy_context(til_module_context_t *context)
{
	meta2d_context_t	*ctxt = (meta2d_context_t *)context;

	din_free(ctxt->din_a);
	din_free(ctxt->din_b);
	free(ctxt);
}


static void meta2d_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	meta2d_context_t	*ctxt = (meta2d_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu };

	if (!til_stream_tap_context(stream, context, NULL, &ctxt->taps.min_t))
		*ctxt->min_t = .7f;

	if (!til_stream_tap_context(stream, context, NULL, &ctxt->taps.max_t))
		*ctxt->max_t = .8f;

	/* move the balls around */
	for (int i = 0; i < META2D_NUM_BALLS; i++) {
		meta2d_ball_t	*ball = &ctxt->balls[i];
		float		rad;

		/* Perlin noise indexed by position for x,y and i for z
		 * is used just for moving the metaballs around.
		 *
		 * Two noise fields are used with their values interpolated,
		 * starting with the din_a being 100% of the movement,
		 * with every frame migrating closer to din_b being 100%.
		 *
		 * Once din_b contributes 100%, it becomes din_a, and the old
		 * din_a becomes din_b which gets randomized, and the % resets
		 * to 0.
		 *
		 * This allows an organic continuous evolution of the field
		 * over time, at double the sampling cost since we're sampling
		 * two noise fields and interpolating them.  Since this is
		 * just per-ball every frame, it's probably OK.  Not like it's
		 * every pixel.
		 */

		/* ad-hoc lerp of the two dins */
		rad = din(ctxt->din_a, &(v3f_t){
			  .x = ball->position.x,
			  .y = ball->position.y,
			  .z = (float)i * (1.f / (float)META2D_NUM_BALLS)}
			 ) * (1.f - ctxt->din_t);

		rad += din(ctxt->din_b, &(v3f_t){
			  .x = ball->position.x,
			  .y = ball->position.y,
			  .z = (float)i * (1.f / (float)META2D_NUM_BALLS)}
			 ) * ctxt->din_t;

		/* Perlin noise doesn't produce anything close to a uniform random distribution
		 * of -1..+1, so it can't just be mapped directly to 2*PI with all angles getting
		 * roughly equal occurrences in the long-run.  For now I just *10.f and it seems
		 * to be OK.
		 */
		rad *= 10.f * 2.f * M_PI;
		v2f_add(&ball->position,
			&ball->position,
			&(v2f_t){
				.x = cosf(rad) * .003f,	/* small steps */
				.y = sinf(rad) * .003f,
			});

		v2f_clamp(&ball->position,
			  &ball->position,
			  &(v2f_t){-.8f, -.8f},	/* keep the balls mostly on-screen */
			  &(v2f_t){.8f, .8f}
			);
	}

	/* when din_t reaches 1 swap a<->b, reset din_t, randomize b */
	ctxt->din_t += .01f;
	if (ctxt->din_t >= 1.f) {
		din_t	*tmp;

		tmp = ctxt->din_a;
		ctxt->din_a = ctxt->din_b;
		ctxt->din_b = tmp;

		din_randomize(ctxt->din_b);
		ctxt->din_t = 0.f;
	}
}


static void meta2d_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	meta2d_context_t	*ctxt = (meta2d_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	float	xf = 2.f / (float)fragment->frame_width;
	float	yf = 2.f / (float)fragment->frame_height;
	v2f_t	coord;

	for (int y = fragment->y; y < fragment->y + fragment->height; y++) {
		coord.y = yf * (float)y - 1.f;

		for (int x = fragment->x; x < fragment->x + fragment->width; x++) {
			v3f_t		color = {};
			uint32_t	pixel;
			float		t = 0;

			coord.x = xf * (float)x - 1.f;

			for (int i = 0; i < META2D_NUM_BALLS; i++) {
				meta2d_ball_t	*ball = &ctxt->balls[i];

				float	f;

				f = ball->radius * ball->radius / v2f_distance_sq(&coord, &ball->position);
				v3f_add(&color, &color, v3f_mult_scalar(&(v3f_t){}, &ball->color, f));
				t += f;
			}

			/* these thresholds define the thickness of the ribbon */
			if (t < *ctxt->min_t || t > *ctxt->max_t)
				color = (v3f_t){};

			pixel = color_to_uint32(color);
			til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, pixel);
		}
	}
}


til_module_t	meta2d_module = {
	.create_context = meta2d_create_context,
	.destroy_context = meta2d_destroy_context,
	.prepare_frame = meta2d_prepare_frame,
	.render_fragment = meta2d_render_fragment,
	.name = "meta2d",
	.description = "Classic 2D metaballs (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
