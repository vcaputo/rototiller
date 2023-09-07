#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_stream.h"
#include "til_tap.h"

#include "ff.h"
#include "v3f.h"

/* Copyright (C) 2017 Vito Caputo <vcaputo@pengaru.com> */

/* TODO:
 * - improve the second pass's element rejection efficiency, a spatial data structure
 *   could probably help here.
 *
 * - rand_element() is called in parallel in the first pass when elements are rebooted,
 *   but a single shared seed is being used.  This should be made a per-cpu seed.
 */

#define FLOW_DEFAULT_SIZE	"8"
#define FLOW_DEFAULT_COUNT	"40000"
#define FLOW_DEFAULT_SPEED	".2"

#define FLOW_MAX_SPEED		40

typedef struct flow_element_t {
	float	lifetime;
	v3f_t	position_a, position_b;
	v3f_t	velocity;	/* per-iter step + direction applicable directly to position_a */
	v3f_t	color;
} flow_element_t;

typedef struct flow_context_t {
	til_module_context_t	til_module_context;

	struct {
		til_tap_t		speed;
	}			taps;

	struct {
		float			speed;
	}			vars;

	float			*speed;


	ff_t			*ff;
	unsigned		last_populate_idx;
	unsigned		n_iters;
	unsigned		n_elements;
	unsigned		n_elements_per_cpu;
	unsigned		pass;
	float			w;
	flow_element_t		elements[];
} flow_context_t;

typedef struct flow_setup_t {
	til_setup_t		til_setup;

	unsigned		size;
	unsigned		count;
	float			speed;
} flow_setup_t;


static void flow_ff_populator(void *context, unsigned size, const ff_data_t *other, ff_data_t *field)
{
	flow_context_t	*ctxt = context;
	unsigned	*seedp = &ctxt->til_module_context.seed;
	unsigned	x, y, z;

	for (x = 0; x < size; x++) {
		for (y = 0; y < size; y++) {
			for (z = 0; z < size; z++) {
				v3f_t	v = v3f_rand(seedp, -1.0f, 1.0f);
				v3f_t	c = v3f_rand(seedp, 0.f, 1.0f);
				size_t	idx = x * size * size + y * size + z;

				field[idx].direction = v3f_lerp(&other[idx].direction, &v, .75f);
				field[idx].color = v3f_lerp(&other[idx].color, &c, .75f);
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
	flow_element_t	e = {
				.lifetime = rand_within_range(seed, .5f, 20.f),
				.position_a = v3f_rand(seed, 0.f, 1.f),
			};

	e.position_a.x = e.position_a.x * 2.f - 1.f;
	e.position_a.y = e.position_a.y * 2.f - 1.f;
	e.position_b = e.position_a;

	return e;
}


static void flow_update_taps(flow_context_t *ctxt, til_stream_t *stream)
{
	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.speed))
		*ctxt->speed = ((flow_setup_t *)ctxt->til_module_context.setup)->speed;
	else
		ctxt->vars.speed = *ctxt->speed;

	if (ctxt->vars.speed < 0.f)
		ctxt->vars.speed = 0.f;

	if (ctxt->vars.speed > 1.f)
		ctxt->vars.speed = 1.f;

	ctxt->n_iters = ceilf(ctxt->vars.speed * FLOW_MAX_SPEED);
}


static til_module_context_t * flow_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	flow_setup_t	*s = (flow_setup_t *)setup;
	flow_context_t	*ctxt;
	unsigned	elements_per_cpu;

	elements_per_cpu  = s->count / n_cpus;
	ctxt = til_module_context_new(module, sizeof(flow_context_t) + sizeof(ctxt->elements[0]) * elements_per_cpu * n_cpus, stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->n_elements_per_cpu = elements_per_cpu;
	ctxt->n_elements = elements_per_cpu * n_cpus;

	ctxt->ff = ff_new(s->size, flow_ff_populator, ctxt);
	if (!ctxt->ff)
		return til_module_context_free(&ctxt->til_module_context);

	for (unsigned i = 0; i < ctxt->n_elements; i++)
		ctxt->elements[i] = rand_element(&ctxt->til_module_context.seed);


	ctxt->taps.speed = til_tap_init_float(ctxt, &ctxt->speed, 1, &ctxt->vars.speed, "speed");
	flow_update_taps(ctxt, stream);

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


static void flow_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	flow_context_t	*ctxt = (flow_context_t *)context;

	switch (ctxt->pass) {
	case 0:
		flow_update_taps(ctxt, stream);

		ctxt->w = (M_2_PI * asinf(fabsf(sinf((ticks * .001f))))) * 2.f - 1.f;
		/* ^^ this approximates a triangle wave,
		 * a sine wave dwells too long for the illusion of continuously evolving
		 */

		*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_noop_per_cpu };
		return;

	case 1:
		*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu };
		return;

	default:
		assert(0);
	}
}


static void flow_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	flow_context_t		*ctxt = (flow_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	switch (ctxt->pass) {
	case 0: {
		flow_element_t	*e = &ctxt->elements[fragment->number * ctxt->n_elements_per_cpu];
		unsigned	n = ctxt->n_elements_per_cpu;
		float		w = ctxt->w * .5f + .5f;

		/* XXX: note the fragment->number is used above as the cpu number, this is to ensure all cpu #s
		 * are actually used.  Since our noop_fragmenter_per_cpu always produces a fragment per cpu,
		 * the fragment->number should exhaust the cpu space.  Relying on the actual cpu number could
		 * skip entire regions of the elements, since there's no guarantee we get scheduled on all CPUs
		 * in a given frame, despite having a fragment per cpu.  An alternative would be to set the
		 * .cpu_affinity flag in the frame_plan, but that just slows things down pointlessly.
		 */

		/* sample the flow-field and update the elements accordingly, splitting ctxt->elements
		 * into elements_per_cpu chunks indexed by cpu, only working on the chunk for this cpu
		 */
		for (unsigned i = 0; i < n; e++, i++) {
			v3f_t		pos;
			ff_data_t	d;

			e->lifetime -= .1f;
			if (e->lifetime <= 0.0f)
				*e = rand_element(&ctxt->til_module_context.seed);

			if (e->position_b.x < -1.f || e->position_b.x > 1.f ||
			    e->position_b.y < -1.f || e->position_b.y > 1.f ||
			    e->position_b.z < 0.f || e->position_b.z > 1.f)
				*e = rand_element(&ctxt->til_module_context.seed);

			pos = e->position_a = e->position_b;

			d = ff_get(ctxt->ff,
				   &(v3f_t){ /* FIXME TODO: just make ff.[ch] use a -1..+1 coordinate system */
					.x = pos.x * .5f + .5f,
					.y = pos.y * .5f + .5f,
					.z = pos.z,
				   }, w);
			e->color = d.color;
			d.direction = v3f_mult_scalar(&d.direction, .001f); /* XXX FIXME: magic number alert! */
			e->velocity = d.direction;

			/* Compute the final position now for the next go-round.
			 * The second pass can't just write it back willy-nilly while racing with others,
			 * despite doing the same thing iteratively as it draws n_iters pixels.  Hence
			 * this position_b becomes position_a situation above.
			 */
			d.direction = v3f_mult_scalar(&d.direction, (float)ctxt->n_iters);
			e->position_b = v3f_add(&pos, &d.direction);
		}

		return;
	}

	case 1: {
		unsigned	ffw = fragment->frame_width,
				ffh = fragment->frame_height;
		unsigned	fx1 = fragment->x,
				fy1 = fragment->y,
				fx2 = fragment->x + fragment->width,
				fy2 = fragment->y + fragment->height;

		til_fb_fragment_clear(fragment);

		/* render elements overlapping with this fragment's tile */
		for (unsigned i = 0; i < ctxt->n_elements; i++) {
			flow_element_t	*e = &ctxt->elements[i];
			v3f_t		pos = e->position_a;
			v3f_t		v = e->velocity;
			unsigned	x1, y1, x2, y2;
			uint32_t	pixel;

			/* Perspective-project the endpoints of the element's travel, this is
			 * the part we can't currently avoid doing per-element per-fragment.
			 */
#define ZCONST 1.0f
			x1 = pos.x / (pos.z + ZCONST) * ffw + (ffw >> 1);
			y1 = pos.y / (pos.z + ZCONST) * ffh + (ffh >> 1) ;
			x2 = e->position_b.x / (e->position_b.z + ZCONST) * ffw + (ffw >> 1);
			y2 = e->position_b.y / (e->position_b.z + ZCONST) * ffh + (ffh >> 1) ;

			/* for cases obviously outside the fragment, don't draw anything */

			/* totally outside (above) */
			if (y1 < fy1 && y2 < fy1)
				continue;

			/* totally outside (below) */
			if (y1 > fy2 && y2 > fy2)
				continue;

			/* totally outside (left) */
			if (x1 < fx1 && x2 < fx1)
				continue;

			/* totally outside (right) */
			if (x1 > fx2 && x2 > fx2)
				continue;

			/* remaining cases draw something, get the pixel ready */
			pixel = color_to_uint32_rgb(e->color);

			/* totally inside, render unchecked */
			if (y1 >= fy1 && y1 < fy2 && y2 >= fy1 && y2 < fy2 &&
			    x1 >= fx1 && x1 < fx2 && x2 >= fx1 && x2 < fx2) {

				(void) til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x1, y1, pixel);
				(void) til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x2, y2, pixel);

				if (!ctxt->n_iters)
					continue;

				for (unsigned j = 1; j < ctxt->n_iters - 1; j++) {

					pos = v3f_add(&pos, &v);

					x1 = pos.x / (pos.z + ZCONST) * ffw + (ffw >> 1);
					y1 = pos.y / (pos.z + ZCONST) * ffh + (ffh >> 1);

					(void) til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x1, y1, pixel);
				}

				continue;
			}

			/* may partially overlap, do same as above but w/checking */
			(void) til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x1, y1, pixel);
			(void) til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x2, y2, pixel);

			if (!ctxt->n_iters)
				continue;

			for (unsigned j = 1; j < ctxt->n_iters - 1; j++) {

				pos = v3f_add(&pos, &v);

				x1 = pos.x / (pos.z + ZCONST) * ffw + (ffw >> 1);
				y1 = pos.y / (pos.z + ZCONST) * ffh + (ffh >> 1);

				(void) til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x1, y1, pixel);
			}
		}

		return;
	}

	default:
		assert(0);
	}
}


static int flow_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned int ticks, til_fb_fragment_t **fragment_ptr)
{
	flow_context_t		*ctxt = (flow_context_t *)context;

	ctxt->pass = (ctxt->pass + 1) % 2;

	if (!ctxt->pass) {
		/* Re-populate the other field before changing directions.
		 * note if the frame rate is too low and we miss a >.95 sample
		 * this will regress to just revisiting the previous field which
		 * is relatively harmless.
		 */
		if (fabsf(ctxt->w) > .95f) {
			unsigned	other_idx;

			other_idx = rintf(-ctxt->w * .5f + .5f);
			if (other_idx != ctxt->last_populate_idx) {
				ff_populate(ctxt->ff, other_idx);
				ctxt->last_populate_idx = other_idx;
			}
		}
	}

	return ctxt->pass;
}


static int flow_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	flow_module = {
	.create_context = flow_create_context,
	.destroy_context = flow_destroy_context,
	.prepare_frame = flow_prepare_frame,
	.render_fragment = flow_render_fragment,
	.finish_frame = flow_finish_frame,
	.setup = flow_setup,
	.name = "flow",
	.description = "3D flow field (threaded)",
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
				"40000",
				"60000",
				"80000",
				"100000",
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
