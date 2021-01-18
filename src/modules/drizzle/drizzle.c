/*
 *  Copyright (C) 2020 - Vito Caputo - <vcaputo@pengaru.com>
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

#include <stdlib.h>
#include <unistd.h>

#include "fb.h"
#include "puddle/puddle.h"
#include "rototiller.h"

#define PUDDLE_SIZE		512
#define DRIZZLE_CNT		20
#define DEFAULT_VISCOSITY	.01f

typedef struct drizzle_context_t {
	puddle_t	*puddle;
	unsigned	n_cpus;
} drizzle_context_t;

typedef struct v3f_t {
	float	x, y, z;
} v3f_t;

typedef struct v2f_t {
	float	x, y;
} v2f_t;

static float	drizzle_viscosity = DEFAULT_VISCOSITY;


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


static void * drizzle_create_context(unsigned ticks, unsigned num_cpus)
{
	drizzle_context_t	*ctxt;

	ctxt = calloc(1, sizeof(drizzle_context_t));
	if (!ctxt)
		return NULL;

	ctxt->puddle = puddle_new(PUDDLE_SIZE, PUDDLE_SIZE);
	if (!ctxt->puddle) {
		free(ctxt);
		return NULL;
	}

	ctxt->n_cpus = num_cpus;

	return ctxt;
}


static void drizzle_destroy_context(void *context)
{
	drizzle_context_t	*ctxt = context;

	puddle_free(ctxt->puddle);
	free(ctxt);
}


static int drizzle_fragmenter(void *context, const fb_fragment_t *fragment, unsigned number, fb_fragment_t *res_fragment)
{
	drizzle_context_t	*ctxt = context;

	return fb_fragment_slice_single(fragment, ctxt->n_cpus, number, res_fragment);
}


static void drizzle_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter)
{
	drizzle_context_t	*ctxt = context;

	*res_fragmenter = drizzle_fragmenter;

	for (int i = 0; i < DRIZZLE_CNT; i++) {
		int	x = rand() % (PUDDLE_SIZE - 1);
		int	y = rand() % (PUDDLE_SIZE - 1);

		/* TODO: puddle should probably offer a normalized way of setting an
		 * area to a value, so if PUDDLE_SIZE changes this automatically
		 * would adapt to cover the same portion of the unit square...
		 */
		puddle_set(ctxt->puddle, x, y, 1.f);
		puddle_set(ctxt->puddle, x + 1, y, 1.f);
		puddle_set(ctxt->puddle, x, y + 1, 1.f);
		puddle_set(ctxt->puddle, x + 1, y + 1, 1.f);
	}

	puddle_tick(ctxt->puddle, drizzle_viscosity);
}


static void drizzle_render_fragment(void *context, unsigned ticks, unsigned cpu, fb_fragment_t *fragment)
{
	drizzle_context_t	*ctxt = context;
	float			xf = 1.f / (float)fragment->frame_width;
	float			yf = 1.f / (float)fragment->frame_height;
	v2f_t			coord;

	coord.y = yf * (float)fragment->y;
	for (int y = fragment->y; y < fragment->y + fragment->height; y++) {

		coord.x = xf * (float)fragment->x;
		for (int x = fragment->x; x < fragment->x + fragment->width; x++) {
			v3f_t		color = {};
			uint32_t	pixel;

			color.z = puddle_sample(ctxt->puddle, &coord);

			pixel = color_to_uint32(color);
			fb_fragment_put_pixel_unchecked(fragment, x, y, pixel);

			coord.x += xf;
		}

		coord.y += yf;
	}
}


static int drizzle_setup(const settings_t *settings, setting_desc_t **next_setting)
{
	const char	*viscosity;
	const char	*values[] = {
				".005f",
				".01f",
				".03f",
				".05f",
				NULL
			};

	viscosity = settings_get_value(settings, "viscosity");
	if (!viscosity) {
		int	r;

		r = setting_desc_clone(&(setting_desc_t){
						.name = "Puddle Viscosity",
						.key = "viscosity",
						.regex = "\\.[0-9]+",
						.preferred = SETTINGS_STR(DEFAULT_VISCOSITY),
						.values = values,
						.annotations = NULL
					}, next_setting);
		if (r < 0)
			return r;

		return 1;
	}

	sscanf(viscosity, "%f", &drizzle_viscosity);

	return 0;
}


rototiller_module_t	drizzle_module = {
	.create_context = drizzle_create_context,
	.destroy_context = drizzle_destroy_context,
	.prepare_frame = drizzle_prepare_frame,
	.render_fragment = drizzle_render_fragment,
	.name = "drizzle",
	.description = "Classic 2D rain effect (threaded (poorly))",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv3",
	.setup = drizzle_setup,
};
