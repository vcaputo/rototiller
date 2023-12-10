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

/*
 * TODO:
 * - make interlace line granularity configurable instead of always 1 pixel
 * - ^^^ same for sine interlacing?
 */

typedef enum mixer_style_t {
	MIXER_STYLE_BLEND,
	MIXER_STYLE_FLICKER,
	MIXER_STYLE_INTERLACE,
	MIXER_STYLE_PAINTROLLER,
	MIXER_STYLE_SINE,
} mixer_style_t;

typedef enum mixer_orientation_t {
	MIXER_ORIENTATION_HORIZONTAL,
	MIXER_ORIENTATION_VERTICAL,
} mixer_orientation_t;

typedef enum mixer_bottom_t {
	MIXER_BOTTOM_A,
	MIXER_BOTTOM_B,
} mixer_bottom_t;

typedef struct mixer_input_t {
	til_module_context_t	*module_ctxt;
	/* XXX: it's expected that inputs will get more settable attributes to stick in here */
} mixer_input_t;

typedef struct mixer_seed_t {
	char			__padding[256];	/* prevent seeds sharing a cache line */
	unsigned		state;
} mixer_seed_t;

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
	mixer_seed_t		seeds[];
} mixer_context_t;

typedef struct mixer_setup_input_t {
	til_setup_t		*setup;
} mixer_setup_input_t;

typedef struct mixer_setup_t {
	til_setup_t		til_setup;

	mixer_style_t		style;
	mixer_setup_input_t	inputs[2];
	mixer_orientation_t	orientation;
	mixer_bottom_t		bottom;
	unsigned		n_passes;
} mixer_setup_t;

#define MIXER_DEFAULT_STYLE		MIXER_STYLE_BLEND
#define MIXER_DEFAULT_PASSES		8
#define MIXER_DEFAULT_ORIENTATION	MIXER_ORIENTATION_VERTICAL
#define MIXER_DEFAULT_BOTTOM		MIXER_BOTTOM_A

static void mixer_update_taps(mixer_context_t *ctxt, til_stream_t *stream, unsigned ticks)
{
	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.T))
		*ctxt->T = cosf(til_ticks_to_rads(ticks)) * .5f + .5f;
	else /* we're not driving the tap, so let's update our local copy just once */
		ctxt->vars.T = *ctxt->T; /* FIXME: taps need synchronization/thread-safe details fleshed out / atomics */
}


static til_module_context_t * mixer_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	mixer_setup_t	*s = (mixer_setup_t *)setup;
	mixer_context_t	*ctxt;
	int		r;

	assert(setup);

	ctxt = til_module_context_new(module, sizeof(mixer_context_t) * (sizeof(mixer_seed_t) * n_cpus), stream, seed, ticks, n_cpus, setup);
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
	mixer_setup_t		*setup = (mixer_setup_t *)context->setup;
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

	case MIXER_STYLE_INTERLACE:
		for (int i = 0; i < context->n_cpus; i++)
			ctxt->seeds[i].state = rand_r(&context->seed);
		/* fallthrough */
	case MIXER_STYLE_SINE:
		/* fallthrough */
	case MIXER_STYLE_PAINTROLLER: {
		float	T = ctxt->vars.T;
		/* INTERLACE and PAINTROLLER progressively overlay b_module output atop a_module,
		 * so we can render b_module into the fragment first.  Only when (T < 1) do we
		 * have to snapshot that then render a_module into the fragment, then the snapshot
		 * of b_module's output can be copied from to overlay the progression.
		 */

		if (T > .001f) {
			til_module_render(ctxt->inputs[setup->bottom == MIXER_BOTTOM_A ? 1 : 0].module_ctxt, stream, ticks, &fragment);

			if (T < .999f)
				ctxt->snapshots[1] = til_fb_fragment_snapshot(&fragment, 0);
		}

		if (T < .999f)
			til_module_render(ctxt->inputs[setup->bottom == MIXER_BOTTOM_A ? 0 : 1].module_ctxt, stream, ticks, &fragment);

		break;
	}

	case MIXER_STYLE_BLEND: {
		float	T = ctxt->vars.T;

		/* BLEND needs *both* contexts rendered and snapshotted for blending,
		 * except when at the start/end points for T.  It's the most costly
		 * style to perform.
		 */
		if (T < .999f) {
			til_module_render(ctxt->inputs[0].module_ctxt, stream, ticks, &fragment);

			if (T > 0.001f)
				ctxt->snapshots[0] = til_fb_fragment_snapshot(&fragment, 0);
		}

		if (T > 0.001f) {
			til_module_render(ctxt->inputs[1].module_ctxt, stream, ticks, &fragment);

			if (T < .999f)
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

		if (T <= 0.001f || T  >= .999f)
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

	case MIXER_STYLE_INTERLACE: {
		til_fb_fragment_t	*snapshot_b;
		float			T = ctxt->vars.T;

		if (T <= 0.001f || T  >= .999f)
			break;

		assert(ctxt->snapshots[1]);

		snapshot_b = ctxt->snapshots[1];

		for (unsigned y = 0; y < fragment->height; y++) {
			float	r = randf(&ctxt->seeds[cpu].state);

			if (r < T)
				til_fb_fragment_copy(fragment, 0, fragment->x, fragment->y + y, fragment->width, 1, snapshot_b);
		}
		break;
	}

	case MIXER_STYLE_PAINTROLLER: {
		mixer_orientation_t	orientation = ((mixer_setup_t *)context->setup)->orientation;
		unsigned		n_passes = ((mixer_setup_t *)context->setup)->n_passes;

		til_fb_fragment_t	*snapshot_b;
		float			T = ctxt->vars.T;
		float			div = 1.f / (float)n_passes;
		unsigned		iwhole = T * n_passes;
		float			frac = T * n_passes - iwhole;

		/* progressively transition from a->b via incremental striping */

		if (T <= 0.001f || T  >= .999f)
			break;

		assert(ctxt->snapshots[1]);

		snapshot_b = ctxt->snapshots[1];

		/* There are two rects to compute:
		 * 1. the whole "rolled" area already transitioned
		 * 2. the in-progress fractional area being rolled
		 *
		 * The simple thing to do is just compute those two in two steps,
		 * and clip their rects to the fragment rect and copy b->fragment
		 * clipped by the result, for each step.  til_fb_fragment_copy()
		 * should clip to the dest fragment for us, so this is rather
		 * trivial.
		 */

		switch (orientation) {
		case MIXER_ORIENTATION_HORIZONTAL: {
			float		row_h = ((float)fragment->frame_height * div);
			unsigned	whole_w = fragment->frame_width;
			unsigned	whole_h = ceilf(row_h * (float)iwhole);
			unsigned	frac_w = ((float)fragment->frame_width * frac);
			unsigned	frac_h = row_h;

			til_fb_fragment_copy(fragment, 0, 0, 0, whole_w, whole_h, snapshot_b);
			til_fb_fragment_copy(fragment, 0, 0, whole_h, frac_w, frac_h, snapshot_b);
			break;
		}

		case MIXER_ORIENTATION_VERTICAL: {
			float		col_w = ((float)fragment->frame_width * div);
			unsigned	whole_w = ceilf(col_w * (float)iwhole);
			unsigned	whole_h = fragment->frame_height;
			unsigned	frac_w = col_w;
			unsigned	frac_h = ((float)fragment->frame_height * frac);

			til_fb_fragment_copy(fragment, 0, 0, 0, whole_w, whole_h, snapshot_b);
			til_fb_fragment_copy(fragment, 0, whole_w, 0, frac_w, frac_h, snapshot_b);
			break;
		}

		default:
			assert(0);
		}

		/* progressively transition from a->b via incremental striping */
		break;
	}

	case MIXER_STYLE_SINE: {
		/* mixer_orientation_t	orientation = ((mixer_setup_t *)context->setup)->orientation; TODO: if vertical is implemented */
		mixer_orientation_t	orientation = MIXER_ORIENTATION_HORIZONTAL;

		til_fb_fragment_t	*snapshot_b;
		float			T = ctxt->vars.T;

		if (T <= 0.001f || T  >= .999f)
			break;

		assert(ctxt->snapshots[1]);

		snapshot_b = ctxt->snapshots[1];

		switch (orientation) {
		case MIXER_ORIENTATION_HORIZONTAL: {
			float	step = (/* TODO: make setting+tap */ 2.f * M_PI) / ((float)fragment->frame_height);
			float	r = til_ticks_to_rads(ticks) /* * 1.f TODO: make setting+tap */  + ((float)fragment->y) * step;

			for (unsigned y = 0; y < fragment->height; y++) {
				int	xoff;
				int	dir = ((y + fragment->y) % 2) ? -1 : 1;

				/* first shift line horizontally by sign-interlaced sine wave */
				xoff = (((cosf(r) * .5f) * (1.f - T))) * dir * (float)fragment->frame_width;

				/* now push apart the opposing sines in proportion to T so snapshot_a can be 100% visible */
				xoff += dir * ((1.f - T) * 1.5 * fragment->frame_width);

				for (unsigned x = 0; x < fragment->width; x++) {
					int	xcoord = xoff + fragment->x + x;

					if (xcoord >= 0 && xcoord < (snapshot_b->x + snapshot_b->width)) {
						uint32_t	pixel;

						pixel = til_fb_fragment_get_pixel_unchecked(snapshot_b, xcoord, fragment->y + y);
						til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, pixel);
					}
				}

				r += step;
			}
			break;
		}

		case MIXER_ORIENTATION_VERTICAL: {
			/* TODO, maybe??
			 * Doing a vertical variant in the obvious manner will be really cache-unfriendly
			 */
			break;
		}

		default:
			assert(0);
		}

		break;
	}

	default:
		assert(0);
	}

	*fragment_ptr = fragment;
}


static int mixer_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned int ticks, til_fb_fragment_t **fragment_ptr)
{
	mixer_context_t	*ctxt = (mixer_context_t *)context;

	for (int i = 0; i < 2; i++) {
		if (!ctxt->snapshots[i])
			continue;

		ctxt->snapshots[i] = til_fb_fragment_reclaim(ctxt->snapshots[i]);
	}

	return 0;
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

	for (size_t i = 0; i < nelems(s->inputs); i++)
		til_setup_free(s->inputs[i].setup);

	free(setup);
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
					"interlace",
					"paintroller",
					"sine",
					NULL
				};
	const char		*passes_values[] = {
					"2",
					"4",
					"6",
					"8",
					"10",
					"12",
					"16",
					"18",
					"20",
					NULL
				};
	const char		*orientation_values[] = {
					"horizontal",
					"vertical",
					NULL
				};
	const char		*bottom_values[] = {
					"a",
					"b",
					NULL
				};
	til_setting_t		*style;
	til_setting_t		*passes;
	til_setting_t		*orientation;
	til_setting_t		*bottom;
	const til_settings_t	*inputs_settings[2];
	til_setting_t		*inputs[2];
	int			r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Mixer blend style",
							.key = "style",
							.values = style_values,
							.preferred = style_values[MIXER_DEFAULT_STYLE],
						},
						&style,
						res_setting,
						res_desc);
	if (r)
		return r;

	/* Though you can simply swap what you provide as a_module and b_module, it's
	 * convenient to have a discrete setting available for specifying which one
	 * goes on the bottom and which one goes on top as well.  Sometimes you're just
	 * exploring mixer styles, and only for some is the "bottom" vs "top"
	 * relevant, and the preference can be style-specific, so just give an
	 * independent easy toggle.
	 */
	if (!strcasecmp(style->value, style_values[MIXER_STYLE_INTERLACE]) ||
	    !strcasecmp(style->value, style_values[MIXER_STYLE_PAINTROLLER]) ||
	    !strcasecmp(style->value, style_values[MIXER_STYLE_SINE])) {

		r = til_settings_get_and_describe_setting(settings,
							&(til_setting_spec_t){
								.name = "Mixer bottom layer",
								.key = "bottom",
								.values = bottom_values,
								.preferred = bottom_values[MIXER_DEFAULT_BOTTOM],
							},
							&bottom,
							res_setting,
							res_desc);
		if (r)
			return r;
	}

	if (!strcasecmp(style->value, style_values[MIXER_STYLE_PAINTROLLER])) {

		r = til_settings_get_and_describe_setting(settings,
							&(til_setting_spec_t){
								.name = "Mixer paint roller orientation",
								.key = "orientation",
								.values = orientation_values,
								.preferred = orientation_values[MIXER_DEFAULT_ORIENTATION],
							},
							&orientation,
							res_setting,
							res_desc);
		if (r)
			return r;

		r = til_settings_get_and_describe_setting(settings,
							&(til_setting_spec_t){
								.name = "Mixer paint roller passes",
								.key = "passes",
								.values = passes_values,
								.preferred = TIL_SETTINGS_STR(MIXER_DEFAULT_PASSES),
							},
							&passes,
							res_setting,
							res_desc);
		if (r)
			return r;
	}

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
					  (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC | TIL_MODULE_AUDIO_ONLY),
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

		r = til_value_to_pos(style_values, style->value, (unsigned *)&setup->style);
		if (r < 0)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, style, res_setting, -EINVAL);

		switch (setup->style) { /* bake any style-specific settings */
		case MIXER_STYLE_PAINTROLLER:
			if (sscanf(passes->value, "%u", &setup->n_passes) != 1)
				return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, passes, res_setting, -EINVAL);

			r = til_value_to_pos(orientation_values, orientation->value, (unsigned *)&setup->orientation);
			if (r < 0)
				return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, orientation, res_setting, -EINVAL);
		/* fallthrough */
		case MIXER_STYLE_INTERLACE:
		/* fallthrough */
		case MIXER_STYLE_SINE:
			r = til_value_to_pos(bottom_values, bottom->value, (unsigned *)&setup->bottom);
			if (r < 0)
				return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, bottom, res_setting, -EINVAL);
			break;

		default:
			break;
		}

		for (i = 0; i < 2; i++) {
			r = til_module_setup_full(inputs_settings[i],
						  res_setting,
						  res_desc,
						  &setup->inputs[i].setup, /* finalize! */
						  input_module_name_names[i],
						  input_preferred[i],
						  (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC | TIL_MODULE_AUDIO_ONLY),
						  exclusions);
			if (r < 0)
				return til_setup_free_with_ret_err(&setup->til_setup, r);

			assert(r == 0);
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}
