#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_stream.h"
#include "til_tap.h"
#include "til_util.h"

/* Copyright (C) 2023 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements a rudimentary mixing module for things like fades */

typedef enum mixer_style_t {
	MIXER_STYLE_BLEND,
	MIXER_STYLE_FLICKER,
} mixer_style_t;

typedef struct mixer_input_t {
	til_module_context_t	*module_ctxt;
	/* XXX: it's expected that inputs will get more settable attributes to stick in here */
} mixer_input_t;

typedef struct mixer_context_t {
	til_module_context_t	til_module_context;

	struct {
		til_tap_t		T;
	}			taps;

	struct {
		float			T;
	}			vars;

	float			*T;

	mixer_input_t		inputs[2];
	til_fb_fragment_t	*snapshots[2];
} mixer_context_t;

typedef struct mixer_setup_input_t {
	til_setup_t		*setup;
} mixer_setup_input_t;

typedef struct mixer_setup_t {
	til_setup_t		til_setup;

	mixer_style_t		style;
	mixer_setup_input_t	inputs[2];
} mixer_setup_t;

#define MIXER_DEFAULT_STYLE	MIXER_STYLE_BLEND


static void mixer_update_taps(mixer_context_t *ctxt, til_stream_t *stream, unsigned ticks)
{
	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.T))
		*ctxt->T = cosf(ticks * .001f) * .5f + .5f;
	else /* we're not driving the tap, so let's update our local copy just once */
		ctxt->vars.T = *ctxt->T; /* FIXME: taps need synchronization/thread-safe details fleshed out / atomics */
}


static til_module_context_t * mixer_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	mixer_setup_t	*s = (mixer_setup_t *)setup;
	mixer_context_t	*ctxt;
	int		r;

	assert(setup);

	ctxt = til_module_context_new(module, sizeof(mixer_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	for (size_t i = 0; i < nelems(s->inputs); i++) {
		const til_module_t	*input_module;

		input_module = ((mixer_setup_t *)setup)->inputs[i].setup->creator;
		r =  til_module_create_context(input_module, stream, rand_r(&seed), ticks, n_cpus, s->inputs[i].setup, &ctxt->inputs[i].module_ctxt);
		if (r < 0)
			return til_module_context_free(&ctxt->til_module_context);
	}

	ctxt->taps.T = til_tap_init_float(ctxt, &ctxt->T, 1, &ctxt->vars.T, "T");
	mixer_update_taps(ctxt, stream, ticks);

	return &ctxt->til_module_context;
}


static void mixer_destroy_context(til_module_context_t *context)
{
	mixer_context_t	*ctxt = (mixer_context_t *)context;

	for (size_t i = 0; i < nelems(ctxt->inputs); i++)
		til_module_context_free(ctxt->inputs[i].module_ctxt);

	free(context);
}


static inline float randf(unsigned *seed)
{
	return 1.f / ((float)RAND_MAX) * rand_r(seed);
}


static void mixer_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	mixer_context_t		*ctxt = (mixer_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;
	size_t			i = 0;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu_x16 };

	mixer_update_taps(ctxt, stream, ticks);

	switch (((mixer_setup_t *)context->setup)->style) {
	case MIXER_STYLE_FLICKER:
		if (randf(&context->seed) < ctxt->vars.T)
			i = 1;
		else
			i = 0;

		til_module_render(ctxt->inputs[i].module_ctxt, stream, ticks, &fragment);
		break;

	case MIXER_STYLE_BLEND: {
		float	T = ctxt->vars.T;

		if (T < 1.f) {
			til_module_render(ctxt->inputs[0].module_ctxt, stream, ticks, &fragment);

			if (T > 0.f)
				ctxt->snapshots[0] = til_fb_fragment_snapshot(&fragment, 0);
		}

		if (T > 0.f) {
			til_module_render(ctxt->inputs[1].module_ctxt, stream, ticks, &fragment);
			if (T < 1.f)
				ctxt->snapshots[1] = til_fb_fragment_snapshot(&fragment, 0);
		}
		break;
	}

	default:
		assert(0);
	}

	*fragment_ptr = fragment;
}


/* derived from modules/drizzle pixel_mult_scalar(), there's definitely room for optimizations */
static inline uint32_t pixels_lerp(uint32_t a_pixel, uint32_t b_pixel, float one_sub_T, float T)
{
	uint32_t	pixel;
	float		a, b;

	/* r */
	a = ((uint8_t)(a_pixel >> 16));
	a *= one_sub_T;
	b = ((uint8_t)(b_pixel >> 16));
	b *= T;

	pixel = (((uint32_t)(a+b)) << 16);

	/* g */
	a = ((uint8_t)(a_pixel >> 8));
	a *= one_sub_T;
	b = ((uint8_t)(b_pixel >> 8));
	b *= T;

	pixel |= (((uint32_t)(a+b)) << 8);

	/* b */
	a = ((uint8_t)a_pixel);
	a *= one_sub_T;
	b = ((uint8_t)b_pixel);
	b *= T;

	pixel |= ((uint32_t)(a+b));

	return pixel;
}


static void mixer_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	mixer_context_t		*ctxt = (mixer_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	switch (((mixer_setup_t *)context->setup)->style) {
	case MIXER_STYLE_FLICKER:
		/* handled in prepare_frame() */
		break;

	case MIXER_STYLE_BLEND: {
		uint32_t		*dest = fragment->buf;
		til_fb_fragment_t	*snapshot_a, *snapshot_b;
		uint32_t		*a, *b;
		float			T = ctxt->vars.T;
		float			one_sub_T = 1.f - T;

		if (T <= 0.f || T  >= 1.f)
			break;

		assert(ctxt->snapshots[0]);
		assert(ctxt->snapshots[1]);

		snapshot_a = ctxt->snapshots[0];
		snapshot_b = ctxt->snapshots[1];
		a = snapshot_a->buf + (fragment->y - snapshot_a->y) * snapshot_a->pitch + (fragment->x - snapshot_a->x);
		b = snapshot_b->buf + (fragment->y - snapshot_b->y) * snapshot_b->pitch + (fragment->x - snapshot_b->x);

		/* for the tweens, we already have snapshots sitting in ctxt->snapshots[],
		 * which we now interpolate the pixels out of in parallel
		 */
		for (unsigned y = 0, h = fragment->height, w = fragment->width; y < h; y++) {
			unsigned x = 0;

			/* go four-wide if there's enough, note even without SSE this is a bit quicker a la unrolled loop */
			if ((w & ~3U)) {
				for (; x < (w & ~3U); x += 4) {
					/* TODO: explore adding a SIMD/SSE implementation, this is an ideal application for it */
					*dest = pixels_lerp(*a, *b, one_sub_T, T);
					dest++;
					a++;
					b++;

					*dest = pixels_lerp(*a, *b, one_sub_T, T);
					dest++;
					a++;
					b++;

					*dest = pixels_lerp(*a, *b, one_sub_T, T);
					dest++;
					a++;
					b++;

					*dest = pixels_lerp(*a, *b, one_sub_T, T);
					dest++;
					a++;
					b++;
				}
			}

			/* pick up any tail pixels */
			for (; x < w; a++, b++, dest++, x++)
				*dest = pixels_lerp(*a, *b, one_sub_T, T);

			a += snapshot_a->pitch - w; /* things are a little awkward because we're fragmenting a threaded render within what was snapshotted */
			b += snapshot_b->pitch - w;
			dest += fragment->stride;
		}

		break;
	}

	default:
		assert(0);
	}

	*fragment_ptr = fragment;
}


static void mixer_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned int ticks, til_fb_fragment_t **fragment_ptr)
{
	mixer_context_t	*ctxt = (mixer_context_t *)context;

	for (int i = 0; i < 2; i++) {
		if (!ctxt->snapshots[i])
			continue;

		ctxt->snapshots[i] = til_fb_fragment_reclaim(ctxt->snapshots[i]);
	}
}


static char * mixer_random_module_setting(unsigned seed)
{
	const char	*candidates[] = {
				"blinds",
				"checkers",
				"drizzle",
				"julia",
				"meta2d",
				"moire",
				"pixbounce",
				"plasma",
				"plato",
				"roto",
				"shapes",
				"snow",
				"sparkler",
				"spiro",
				"stars",
				"submit",
				"swab",
				"swarm",
				"voronoi",
			};

	return strdup(candidates[rand() % nelems(candidates)]);
}


static void mixer_setup_free(til_setup_t *setup)
{
	mixer_setup_t	*s = (mixer_setup_t *)setup;

	if (s) {
		for (size_t i = 0; i < nelems(s->inputs); i++)
			til_setup_free(s->inputs[i].setup);

		free(setup);
	}
}


static int mixer_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	mixer_module = {
	.create_context = mixer_create_context,
	.destroy_context = mixer_destroy_context,
	.prepare_frame = mixer_prepare_frame,
	.render_fragment = mixer_render_fragment,
	.finish_frame = mixer_finish_frame,
	.name = "mixer",
	.description = "Module blender (threaded)",
	.setup = mixer_setup,
};


static int mixer_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char		*input_names[2] = { "First module to mix", "Second module to mix" };
	const char		*input_keys[2] = { "a_module", "b_module" };
	const char		*input_module_name_names[2] = { "First module's name", "Second module's name" };
	const char		*input_preferred[2] = { "blank", "compose" };
	const char		*exclusions[] = { "none", "mixer", NULL };

	const char		*style_values[] = {
					"blend",
					"flicker",
					NULL
				};
	til_setting_t		*style;
	const til_settings_t	*inputs_settings[2];
	til_setting_t		*inputs[2];
	int			r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Mixer blend style",
							.key = "style",
							.values = style_values,
							.preferred = style_values[MIXER_DEFAULT_STYLE],
							.annotations = NULL
						},
						&style,
						res_setting,
						res_desc);
	if (r)
		return r;

	for (int i = 0; i < 2; i++) {
		r = til_settings_get_and_describe_setting(settings,
							&(til_setting_spec_t){
								.name = input_names[i],
								.key = input_keys[i],
								.preferred = input_preferred[i],
								.as_nested_settings = 1,
								.random = mixer_random_module_setting,
							},
							&inputs[i],
							res_setting,
							res_desc);
		if (r)
			return r;

		inputs_settings[i] = (*res_setting)->value_as_nested_settings;
		assert(inputs_settings[i]);

		r = til_module_setup_full(inputs_settings[i],
					  res_setting,
					  res_desc,
					  NULL, /* XXX: no res_setup, defer finalizing */
					  input_module_name_names[i],
					  input_preferred[i],
					  (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC),
					  exclusions);
		if (r)
			return r;
	}

	if (res_setup) {
		mixer_setup_t	*setup;
		unsigned	i;

		setup = til_setup_new(settings, sizeof(*setup), mixer_setup_free, &mixer_module);
		if (!setup)
			return -ENOMEM;

		for (i = 0; style_values[i]; i++) {
			if (!strcasecmp(style_values[i], style->value)) {
				setup->style = i;
				break;
			}
		}

		if (!style_values[i])
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, style, res_setting, -EINVAL);

		for (i = 0; i < 2; i++) {
			r = til_module_setup_full(inputs_settings[i],
						  res_setting,
						  res_desc,
						  &setup->inputs[i].setup, /* finalize! */
						  input_module_name_names[i],
						  input_preferred[i],
						  (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC),
						  exclusions);
			if (r < 0)
				return til_setup_free_with_ret_err(&setup->til_setup, r);

			assert(r == 0);
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}
