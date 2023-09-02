#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"

#include "ff.h"
#include "v3f.h"

/* Copyright (C) 2017 Vito Caputo <vcaputo@pengaru.com> */

/* TODO:
 * - make threaded
 * - make colorful
 */

#define FLOW_DEFAULT_SIZE	"8"
#define FLOW_DEFAULT_COUNT	"30000"
#define FLOW_DEFAULT_SPEED	".2"

#define FLOW_MAX_SPEED		40

typedef struct flow_element_t {
	float	lifetime;
	v3f_t	position;
} flow_element_t;

typedef struct flow_context_t {
	til_module_context_t	til_module_context;
	ff_t			*ff;
	unsigned		last_populate_idx;
	unsigned		n_iters;
	unsigned		n_elements;
	flow_element_t		elements[];
} flow_context_t;

typedef struct flow_setup_t {
	til_setup_t		til_setup;

	unsigned		size;
	unsigned		count;
	float			speed;
} flow_setup_t;


static void populator(void *context, unsigned size, const v3f_t *other, v3f_t *field)
{
	flow_context_t	*ctxt = context;
	unsigned	*seedp = &ctxt->til_module_context.seed;
	unsigned	x, y, z;

	for (x = 0; x < size; x++) {
		for (y = 0; y < size; y++) {
			for (z = 0; z < size; z++) {
				v3f_t	v = v3f_rand(seedp, -1.0f, 1.0f);
				size_t	idx = x * size * size + y * size + z;

				field[idx] = v3f_lerp(&other[idx], &v, .75f);
			}
		}
	}
}


static inline float rand_within_range(unsigned *seed, float min, float max)
{
	return (min + ((float)rand_r(seed) * (1.0f/RAND_MAX)) * (max - min));
}


static inline flow_element_t rand_element(unsigned *seed)
{
	flow_element_t	e;

	e.lifetime = rand_within_range(seed, 0.5f, 20.0f);
	e.position = v3f_rand(seed, 0.0f, 1.0f);

	return e;
}


static til_module_context_t * flow_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	flow_setup_t	*s = (flow_setup_t *)setup;
	flow_context_t	*ctxt;
	unsigned	i;

	ctxt = til_module_context_new(module, sizeof(flow_context_t) + sizeof(ctxt->elements[0]) * s->count, stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->ff = ff_new(s->size, populator, ctxt);
	if (!ctxt->ff)
		return til_module_context_free(&ctxt->til_module_context);

	for (i = 0; i < s->count; i++)
		ctxt->elements[i] = rand_element(&ctxt->til_module_context.seed);

	ctxt->n_iters = ceilf(s->speed * FLOW_MAX_SPEED);
	ctxt->n_elements = s->count;

	return &ctxt->til_module_context;
}


static void flow_destroy_context(til_module_context_t *context)
{
	flow_context_t	*ctxt = (flow_context_t *)context;

	ff_free(ctxt->ff);
	free(context);
}


static inline uint32_t color_to_uint32_rgb(v3f_t color) {
	uint32_t        pixel;

	/* doing this all per-pixel, ugh. */

	color = v3f_clamp_scalar(0.0f, 1.0f, &color);

	pixel = (uint32_t)(color.x * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.y * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.z * 255.0f);

	return pixel;
}


static void flow_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	flow_context_t		*ctxt = (flow_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;
	float			w;

	til_fb_fragment_clear(fragment);

	w = (M_2_PI * asinf(fabsf(sinf((ticks * .001f))))) * 2.f - 1.f;
	/* ^^ this approximates a triangle wave,
	 * a sine wave dwells too long for the illusion of continuously evolving
	 */

	for (unsigned j = 0; j < ctxt->n_elements; j++) {
		flow_element_t	*e = &ctxt->elements[j];
		v3f_t		pos = e->position;
		v3f_t		v = ff_get(ctxt->ff, &pos, w * .5f + .5f);

		v = v3f_mult_scalar(&v, .001f);

		for (unsigned k = 0; k < ctxt->n_iters; k++) {
			unsigned	x, y;
			v3f_t		color;

			pos = v3f_add(&pos, &v);
#define ZCONST 1.0f
			x = (pos.x * 2.f - 1.f) / (pos.z + ZCONST) * fragment->width + (fragment->width >> 1);
			y = (pos.y * 2.f - 1.f) / (pos.z + ZCONST) * fragment->height + (fragment->height >> 1) ;

			color.x = color.y = color.z = e->lifetime;

			if (!til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, color_to_uint32_rgb(color)) ||
			    pos.x < 0.f || pos.x > 1.f ||
			    pos.y < 0.f || pos.y > 1.f ||
			    pos.z < 0.f || pos.z > 1.f)
				*e = rand_element(&ctxt->til_module_context.seed);
			else
				e->position = pos;
		}

		e->lifetime -= .1f;
		if (e->lifetime <= 0.0f)
			*e = rand_element(&ctxt->til_module_context.seed);
	}

	/* Re-populate the other field before changing directions.
	 * note if the frame rate is too low and we miss a >.95 sample
	 * this will regress to just revisiting the previous field which
	 * is relatively harmless.
	 */
	if (fabsf(w) > .95f) {
		unsigned	other_idx;

		other_idx = rintf(-w * .5f + .5f);
		if (other_idx != ctxt->last_populate_idx) {
			ff_populate(ctxt->ff, other_idx);
			ctxt->last_populate_idx = other_idx;
		}
	}
}


static int flow_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	flow_module = {
	.create_context = flow_create_context,
	.destroy_context = flow_destroy_context,
	.render_fragment = flow_render_fragment,
	.setup = flow_setup,
	.name = "flow",
	.description = "3D flow field",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


static int flow_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*size;
	const char	*size_values[] = {
				"2",
				"4",
				"8",
				"16",
				"32",
				NULL
			};
	til_setting_t	*count;
	const char	*count_values[] = {
				"100",
				"1000",
				"5000",
				"10000",
				"20000",
				"30000",
				"40000",
				"50000",
				NULL
			};
	til_setting_t	*speed;
	const char	*speed_values[] = {
				".02",
				".04",
				".08",
				".16",
				".2",
				".4",
				".6",
				".8",
				".9",
				"1",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Size of flow field cube",
							.key = "size",
							.regex = "\\[0-9]+", /* FIXME */
							.preferred = FLOW_DEFAULT_SIZE,
							.values = size_values,
							.annotations = NULL
						},
						&size,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Count of flowing elements",
							.key = "count",
							.regex = "\\[0-9]+", /* FIXME */
							.preferred = FLOW_DEFAULT_COUNT,
							.values = count_values,
							.annotations = NULL
						},
						&count,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Speed of all flow through field",
							.key = "speed",
							.regex = "\\.[0-9]+", /* FIXME */
							.preferred = FLOW_DEFAULT_SPEED,
							.values = speed_values,
							.annotations = NULL
						},
						&speed,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		flow_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &flow_module);
		if (!setup)
			return -ENOMEM;

		if (sscanf(size->value, "%u", &setup->size) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, size, res_setting, -EINVAL);

		if (sscanf(count->value, "%u", &setup->count) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, count, res_setting, -EINVAL);

		if (sscanf(speed->value, "%f", &setup->speed) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, speed, res_setting, -EINVAL);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
