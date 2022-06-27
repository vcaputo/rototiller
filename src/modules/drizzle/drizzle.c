/*
 *  Copyright (C) 2020 - Vito Caputo - <vcaputo@pengaru.com>
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

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

#include "puddle/puddle.h"

#define PUDDLE_SIZE		512
#define DRIZZLE_CNT		20
#define DEFAULT_VISCOSITY	.01

typedef struct v3f_t {
	float	x, y, z;
} v3f_t;

typedef struct v2f_t {
	float	x, y;
} v2f_t;

typedef struct drizzle_setup_t {
	til_setup_t	til_setup;
	float		viscosity;
} drizzle_setup_t;

typedef struct drizzle_context_t {
	til_module_context_t	til_module_context;
	til_fb_fragment_t	*snapshot;
	puddle_t		*puddle;
	drizzle_setup_t		setup;
} drizzle_context_t;

static drizzle_setup_t drizzle_default_setup = {
	.viscosity = DEFAULT_VISCOSITY,
};


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


static til_module_context_t * drizzle_create_context(unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	drizzle_context_t	*ctxt;

	if (!setup)
		setup = &drizzle_default_setup.til_setup;

	ctxt = til_module_context_new(sizeof(drizzle_context_t), seed, ticks, n_cpus);
	if (!ctxt)
		return NULL;

	ctxt->puddle = puddle_new(PUDDLE_SIZE, PUDDLE_SIZE);
	if (!ctxt->puddle) {
		free(ctxt);
		return NULL;
	}

	ctxt->setup = *(drizzle_setup_t *)setup;

	return &ctxt->til_module_context;
}


static void drizzle_destroy_context(til_module_context_t *context)
{
	drizzle_context_t	*ctxt = (drizzle_context_t *)context;

	puddle_free(ctxt->puddle);
	free(ctxt);
}


static void drizzle_prepare_frame(til_module_context_t *context, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	drizzle_context_t	*ctxt = (drizzle_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu };

	for (int i = 0; i < DRIZZLE_CNT; i++) {
		int	x = rand_r(&ctxt->til_module_context.seed) % (PUDDLE_SIZE - 1);
		int	y = rand_r(&ctxt->til_module_context.seed) % (PUDDLE_SIZE - 1);

		/* TODO: puddle should probably offer a normalized way of setting an
		 * area to a value, so if PUDDLE_SIZE changes this automatically
		 * would adapt to cover the same portion of the unit square...
		 */
		puddle_set(ctxt->puddle, x, y, 1.f);
		puddle_set(ctxt->puddle, x + 1, y, 1.f);
		puddle_set(ctxt->puddle, x, y + 1, 1.f);
		puddle_set(ctxt->puddle, x + 1, y + 1, 1.f);
	}

	puddle_tick(ctxt->puddle, ctxt->setup.viscosity);

	if ((*fragment_ptr)->cleared)
		ctxt->snapshot = til_fb_fragment_snapshot(fragment_ptr, 0);
}


/* TODO: this probably should also go through a gamma correction */
static inline uint32_t pixel_mult_scalar(uint32_t pixel, float t)
{
	float	r, g, b;

	if (t > 1.f)
		t = 1.f;
	if (t < 0.f)
		t = 0.f;

	r = (pixel >> 16) & 0xff;
	g = (pixel >> 8) & 0xff;
	b = (pixel & 0xff);

	r *= t;
	g *= t;
	b *= t;

	return	((uint32_t)r) << 16 | ((uint32_t)g) << 8 | ((uint32_t)b);
}


static void drizzle_render_fragment(til_module_context_t *context, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	drizzle_context_t	*ctxt = (drizzle_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	float			xf = 1.f / (float)fragment->frame_width;
	float			yf = 1.f / (float)fragment->frame_height;
	v2f_t			coord;

	if (ctxt->snapshot) {
		coord.y = yf * (float)fragment->y;
		for (int y = fragment->y; y < fragment->y + fragment->height; y++) {

			coord.x = xf * (float)fragment->x;
			for (int x = fragment->x; x < fragment->x + fragment->width; x++) {
				float		t = puddle_sample(ctxt->puddle, &coord);
				uint32_t	pixel = pixel_mult_scalar(til_fb_fragment_get_pixel_unchecked(ctxt->snapshot, x, y), t);

				til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, pixel);

				coord.x += xf;
			}

			coord.y += yf;
		}

		return;
	}

	coord.y = yf * (float)fragment->y;
	for (int y = fragment->y; y < fragment->y + fragment->height; y++) {

		coord.x = xf * (float)fragment->x;
		for (int x = fragment->x; x < fragment->x + fragment->width; x++) {
			v3f_t		color = {};
			uint32_t	pixel;

			color.z = puddle_sample(ctxt->puddle, &coord);

			pixel = color_to_uint32(color);
			til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, pixel);

			coord.x += xf;
		}

		coord.y += yf;
	}
}


static void drizzle_finish_frame(til_module_context_t *context, unsigned int ticks, til_fb_fragment_t **fragment_ptr)
{
	drizzle_context_t	*ctxt = (drizzle_context_t *)context;

	if (ctxt->snapshot)
		ctxt->snapshot = til_fb_fragment_reclaim(ctxt->snapshot);
}


static int drizzle_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*viscosity;
	const char	*values[] = {
				".005",
				".01",
				".03",
				".05",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Puddle viscosity",
							.key = "viscosity",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(DEFAULT_VISCOSITY),
							.values = values,
							.annotations = NULL
						},
						&viscosity,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		drizzle_setup_t	*setup;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		sscanf(viscosity, "%f", &setup->viscosity);

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	drizzle_module = {
	.create_context = drizzle_create_context,
	.destroy_context = drizzle_destroy_context,
	.prepare_frame = drizzle_prepare_frame,
	.render_fragment = drizzle_render_fragment,
	.finish_frame = drizzle_finish_frame,
	.name = "drizzle",
	.description = "Classic 2D rain effect (threaded (poorly))",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.setup = drizzle_setup,
	.flags = TIL_MODULE_OVERLAYABLE,
};
