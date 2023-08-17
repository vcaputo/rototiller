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
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_stream.h"
#include "til_tap.h"

#include "puddle/puddle.h"

/* TODO: make size a setting from like 128-1024, and cnt something settable for a fraction per frame (one every Nth frame) up to 20 per frame, though it should probably be less framerate dependent */
#define PUDDLE_SIZE		512
#define RAINFALL_CNT		20
#define DEFAULT_VISCOSITY	.01
#define DEFAULT_STYLE		DRIZZLE_STYLE_MASK

typedef enum drizzle_style_t {
	DRIZZLE_STYLE_MASK,
	DRIZZLE_STYLE_MAP,
} drizzle_style_t;

typedef struct v3f_t {
	float	x, y, z;
} v3f_t;

typedef struct v2f_t {
	float	x, y;
} v2f_t;

typedef struct drizzle_setup_t {
	til_setup_t	til_setup;
	float		viscosity;
	drizzle_style_t	style;
} drizzle_setup_t;

typedef struct drizzle_context_t {
	til_module_context_t	til_module_context;
	struct {
		til_tap_t	viscosity, rainfall;
	} taps;

	struct {
		float		viscosity, rainfall;
	} vars;

	float			*viscosity, *rainfall;
	til_fb_fragment_t	*snapshot;
	puddle_t		*puddle;
	drizzle_setup_t		*setup;
} drizzle_context_t;


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


static void drizzle_update_taps(drizzle_context_t *ctxt, til_stream_t *stream, unsigned ticks)
{
	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.viscosity))
		*ctxt->viscosity = ctxt->setup->viscosity;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.rainfall))
		*ctxt->rainfall = RAINFALL_CNT;
}


static til_module_context_t * drizzle_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	drizzle_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(drizzle_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->puddle = puddle_new(PUDDLE_SIZE, PUDDLE_SIZE);
	if (!ctxt->puddle) {
		free(ctxt);
		return NULL;
	}

	ctxt->taps.viscosity = til_tap_init_float(ctxt, &ctxt->viscosity, 1, &ctxt->vars.viscosity, "viscosity");
	ctxt->taps.rainfall = til_tap_init_float(ctxt, &ctxt->rainfall, 1, &ctxt->vars.rainfall, "rainfall");

	ctxt->setup = (drizzle_setup_t *)setup;

	drizzle_update_taps(ctxt, stream, ticks);

	return &ctxt->til_module_context;
}


static void drizzle_destroy_context(til_module_context_t *context)
{
	drizzle_context_t	*ctxt = (drizzle_context_t *)context;

	puddle_free(ctxt->puddle);
	free(ctxt);
}


static void drizzle_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	drizzle_context_t	*ctxt = (drizzle_context_t *)context;

	drizzle_update_taps(ctxt, stream, ticks);

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu_x16 };

	for (unsigned i = 0; i < (unsigned)*ctxt->rainfall; i++) {
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

	puddle_tick(ctxt->puddle, *ctxt->viscosity);

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


/* TOD: libs should probably just get v[23]f.h added already instead of constantly
 * duplicating these
 */
static inline float v3f_dot(const v3f_t *a, const v3f_t *b)
{
	return (a->x * b->x + a->y * b->y + a->z * b->z);
}


static inline float v3f_len(const v3f_t *v)
{
	return sqrtf(v3f_dot(v, v));
}


static inline v3f_t * v3f_mult_scalar(v3f_t *res, const v3f_t *v, float s)
{
	res->x = v->x * s;
	res->y = v->y * s;
	res->z = v->z * s;

	return res;
}


static inline v3f_t * v3f_norm(v3f_t *res, const v3f_t *v)
{
	return v3f_mult_scalar(res, v, 1.f / v3f_len(v));
}


static inline v3f_t * v3f_cross(v3f_t *res, const v3f_t *a, const v3f_t *b)
{
	res->x = a->y * b->z - a->z * b->y;
	res->y = a->z * b->x - a->x * b->z;
	res->z = a->x * b->y - a->y * b->x;

	return res;
}


/* Similar to puddle_sample() except instead of returning an interpolated scalar
 * a 3d normal vector is produced by treating the normally interpolated values as
 * gradient samples on a 2d height map.
 */
static void puddle_sample_normal(const puddle_t *puddle, const v2f_t *coordinate, v3f_t *res_normal)
{
	float	s0, sa, sb;

	/* take three samples surrounding coordinate to create gradient vectors */
	s0 = puddle_sample(puddle,	&(v2f_t){
						.x = coordinate->x,
						.y = coordinate->y - .0001f	/* TODO: when PUDDLE_SIZE is small these need to be larger, revisit when size becomes runtime settable */
					});

	sa = puddle_sample(puddle,	&(v2f_t){
						.x = coordinate->x - .0001f,
						.y = coordinate->y + .0001f
					});

	sb = puddle_sample(puddle,	&(v2f_t){
						.x = coordinate->x + .0001f,
						.y = coordinate->y + .0001f
					});

	/* cross product them to produce a normal */
	(void) v3f_norm(res_normal,
		v3f_cross(&(v3f_t){},
			&(v3f_t){
				.x = -.0001f,
				.y = .0002f,
				.z = sa - s0
			},
			&(v3f_t){
				.x = .0001f,
				.y = .0002f,
				.z = sb - s0
			}
		)
	);
}


static void drizzle_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	drizzle_context_t	*ctxt = (drizzle_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	float			xf = 1.f / (float)fragment->frame_width;
	float			yf = 1.f / (float)fragment->frame_height;
	v2f_t			coord;

	if (!ctxt->snapshot) {
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
		return;
	}

	switch (ctxt->setup->style) {
	case DRIZZLE_STYLE_MASK:
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

	case DRIZZLE_STYLE_MAP:
		coord.y = yf * (float)fragment->y;
		for (int y = fragment->y; y < fragment->y + fragment->height; y++) {

			coord.x = xf * (float)fragment->x;
			for (int x = fragment->x; x < fragment->x + fragment->width; x++) {
				v3f_t		norm;
				uint32_t	pixel;

				puddle_sample_normal(ctxt->puddle, &coord, &norm);
				//printf("norm.x=%f norm.y=%f norm.z=%f\n", norm.x, norm.y, norm.z);

				pixel = til_fb_fragment_get_pixel_clipped(ctxt->snapshot, x + (norm.x * 10.f), y + (norm.y * 10.f));
				pixel = pixel_mult_scalar(pixel, 1.f - v3f_dot(&norm, &(v3f_t){.x = 0.f, .y = 0, .z = -1.f}));

				til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, pixel);

				coord.x += xf;
			}

			coord.y += yf;
		}

		return;
	}

}


static void drizzle_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned int ticks, til_fb_fragment_t **fragment_ptr)
{
	drizzle_context_t	*ctxt = (drizzle_context_t *)context;

	if (ctxt->snapshot)
		ctxt->snapshot = til_fb_fragment_reclaim(ctxt->snapshot);
}


static int drizzle_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


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


static int drizzle_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*viscosity;
	const char	*style;
	const char	*viscosity_values[] = {
				".005",
				".01",
				".03",
				".05",
				NULL
			};
	const char	*style_values[] = {
				"mask",
				"map",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Puddle viscosity",
							.key = "viscosity",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(DEFAULT_VISCOSITY),
							.values = viscosity_values,
							.annotations = NULL
						},
						&viscosity,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Overlay style",
							.key = "style",
							.regex = "[a-z]+",
							.preferred = style_values[DEFAULT_STYLE],
							.values = style_values,
							.annotations = NULL
						},
						&style,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		drizzle_setup_t	*setup;
		int		i;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &drizzle_module);
		if (!setup)
			return -ENOMEM;

		sscanf(viscosity, "%f", &setup->viscosity);

		/* TODO: til should prolly have a helper for this */
		for (i = 0; i < nelems(style_values); i++) {
			if (!strcasecmp(style_values[i], style)) {
				setup->style = i;
				break;
			}
		}

		if (i >= nelems(style_values))
			return -EINVAL;

		*res_setup = &setup->til_setup;
	}

	return 0;
}
