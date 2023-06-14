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

	size_t			n_inputs;
	mixer_input_t		inputs[];
} mixer_context_t;

typedef struct mixer_setup_input_t {
	char			*module;
	til_setup_t		*setup;
} mixer_setup_input_t;

typedef struct mixer_setup_t {
	til_setup_t		til_setup;

	mixer_style_t		style;
	size_t			n_inputs;
	mixer_setup_input_t	inputs[];
} mixer_setup_t;

#define MIXER_DEFAULT_STYLE	MIXER_STYLE_FLICKER

static til_module_context_t * mixer_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup);
static void mixer_destroy_context(til_module_context_t *context);
static void mixer_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr);
static int mixer_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	mixer_module = {
	.create_context = mixer_create_context,
	.destroy_context = mixer_destroy_context,
	.render_fragment = mixer_render_fragment,
	.name = "mixer",
	.description = "Module blender",
	.setup = mixer_setup,
	.flags = TIL_MODULE_EXPERIMENTAL,
};


static til_module_context_t * mixer_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	mixer_setup_t	*s = (mixer_setup_t *)setup;
	mixer_context_t	*ctxt;
	int		r;

	assert(setup);

	ctxt = til_module_context_new(module, sizeof(mixer_context_t) + s->n_inputs * sizeof(mixer_input_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	for (size_t i = 0; i < s->n_inputs; i++) {
		const til_module_t	*input_module;
		input_module = til_lookup_module(((mixer_setup_t *)setup)->inputs[i].module);
		r =  til_module_create_context(input_module, stream, rand_r(&seed), ticks, n_cpus, s->inputs[i].setup, &ctxt->inputs[i].module_ctxt);
		if (r < 0)
			return til_module_context_free(&ctxt->til_module_context);

		ctxt->n_inputs++;
	}

	ctxt->taps.T = til_tap_init_float(ctxt, &ctxt->T, 1, &ctxt->vars.T, "T");

	return &ctxt->til_module_context;
}


static void mixer_destroy_context(til_module_context_t *context)
{
	mixer_context_t	*ctxt = (mixer_context_t *)context;

	for (size_t i = 0; i < ctxt->n_inputs; i++)
		til_module_context_free(ctxt->inputs[i].module_ctxt);

	free(context);
}

static inline float randf(unsigned *seed)
{
	return 1.f / ((float)RAND_MAX) * rand_r(seed);
}

static void mixer_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	mixer_context_t		*ctxt = (mixer_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;
	size_t			i = 0;

	if (!til_stream_tap_context(stream, context, NULL, &ctxt->taps.T))
		*ctxt->T = cosf(ticks * .001f) * .5f + .5f;

	switch (((mixer_setup_t *)context->setup)->style) {
	case MIXER_STYLE_FLICKER:
		if (randf(&context->seed) < *ctxt->T)
			i = 1;
		else
			i = 0;
		break;

	default:
		assert(0);
	}

	//for (size_t i = 0; i < ctxt->n_inputs; i++)
	til_module_render(ctxt->inputs[i].module_ctxt, stream, ticks, &fragment);

	*fragment_ptr = fragment;
}


static void mixer_setup_free(til_setup_t *setup)
{
	mixer_setup_t	*s = (mixer_setup_t *)setup;

	if (s) {
		for (size_t i = 0; i < s->n_inputs; i++) {
			free(s->inputs[i].module);
			til_setup_free(s->inputs[i].setup);
		}
		free(setup);
	}
}


static int mixer_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char		*style_values[] = {
					"flicker",
					NULL
				};
	const char		*style;
	const til_settings_t	*inputs_settings;
	const char		*inputs;
	int			r;

	r = til_settings_get_and_describe_value(settings,
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

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Comma-separated list of module inputs",
							.key = "inputs",
							.preferred = "stars,plato",
							.annotations = NULL,
							//.random = mixer_random_inputs_setting,
							.as_nested_settings = 1,
						},
						&inputs, /* XXX: unused in raw-value form, we want the settings instance */
						res_setting,
						res_desc);
	if (r)
		return r;

	assert(res_setting && *res_setting && (*res_setting)->value_as_nested_settings);
	inputs_settings = (*res_setting)->value_as_nested_settings;
	{
		til_setting_t	*input_setting;

		for (size_t i = 0; til_settings_get_value_by_idx(inputs_settings, i, &input_setting); i++) {
			if (!input_setting->value_as_nested_settings) {
				r = til_setting_desc_new(	inputs_settings,
								&(til_setting_spec_t){
									.as_nested_settings = 1,
								}, res_desc);
				if (r < 0)
					return r;

				*res_setting = input_setting;

				return 1;
			}
		}

		for (size_t i = 0; til_settings_get_value_by_idx(inputs_settings, i, &input_setting); i++) {
			til_setting_t		*input_module_setting;
			const char		*input = til_settings_get_value_by_idx(input_setting->value_as_nested_settings, 0, &input_module_setting);
			const til_module_t	*input_module;

			if (!input_module_setting)
				return -EINVAL;

			if (!input_module_setting->desc) {
				r = til_setting_desc_new(	input_setting->value_as_nested_settings,
								&(til_setting_spec_t){
									.name = "Input module name",
									.preferred = "none",
									.as_label = 1,
								}, res_desc);
				if (r < 0)
					return r;

				*res_setting = input_module_setting;

				return 1;
			}

			input_module = til_lookup_module(input);
			if (!input_module)
				return -EINVAL;

			if (input_module->setup) {
				r = input_module->setup(input_setting->value_as_nested_settings, res_setting, res_desc, NULL);
				if (r)
					return r;
			}
		}
	}

	if (res_setup) {
		size_t			n_inputs = til_settings_get_count(inputs_settings);
		til_setting_t		*input_setting;
		mixer_setup_t		*setup;

		setup = til_setup_new(settings, sizeof(*setup) + n_inputs * sizeof(setup->inputs[0]), mixer_setup_free);
		if (!setup)
			return -ENOMEM;

		for (int i = 0; style_values[i]; i++) {
			if (!strcasecmp(style_values[i], style))
				setup->style = i;
		}

		setup->n_inputs = n_inputs;

		for (size_t i = 0; til_settings_get_value_by_idx(inputs_settings, i, &input_setting); i++) {
			const char		*input = til_settings_get_value_by_idx(input_setting->value_as_nested_settings, 0, NULL);
			const til_module_t	*input_module;

			setup->inputs[i].module = strdup(input);
			if (!setup->inputs[i].module) {
				til_setup_free(&setup->til_setup);

				return -ENOMEM;
			}

			input_module = til_lookup_module(input);
			if (!input_module) {
				til_setup_free(&setup->til_setup);

				return -EINVAL;
			}

			r = til_module_setup_finalize(input_module, input_setting->value_as_nested_settings, &setup->inputs[i].setup);
			if (r < 0) {
				til_setup_free(&setup->til_setup);

				return r;
			}
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}
