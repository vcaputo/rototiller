#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_util.h"

/* Copyright (C) 2020 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements a rudimentary compositing module for layering
 * the output from other modules into a single frame.
 */

typedef struct compose_layer_t {
	til_module_context_t	*module_ctxt;
	/* XXX: it's expected that layers will get more settable attributes to stick in here */
} compose_layer_t;

typedef struct compose_context_t {
	til_module_context_t	til_module_context;

	til_fb_fragment_t	texture_fb;
	compose_layer_t		texture;
	size_t			n_layers;
	compose_layer_t		layers[];
} compose_context_t;

typedef struct compose_setup_layer_t {
	char			*module;
	til_setup_t		*setup;
} compose_setup_layer_t;

typedef struct compose_setup_t {
	til_setup_t		til_setup;
	compose_setup_layer_t	texture;
	size_t			n_layers;
	compose_setup_layer_t	layers[];
} compose_setup_t;

static til_module_context_t * compose_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup);
static void compose_destroy_context(til_module_context_t *context);
static void compose_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr);
static int compose_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	compose_module = {
	.create_context = compose_create_context,
	.destroy_context = compose_destroy_context,
	.render_fragment = compose_render_fragment,
	.name = "compose",
	.description = "Layered modules compositor",
	.setup = compose_setup,
};


static til_module_context_t * compose_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	compose_setup_t		*s = (compose_setup_t *)setup;
	compose_context_t	*ctxt;

	assert(setup);

	ctxt = til_module_context_new(module, sizeof(compose_context_t) + s->n_layers * sizeof(compose_layer_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	for (size_t i = 0; i < s->n_layers; i++) {
		const til_module_t	*layer_module;

		layer_module = til_lookup_module(((compose_setup_t *)setup)->layers[i].module);
		(void) til_module_create_context(layer_module, stream, rand_r(&seed), ticks, n_cpus, s->layers[i].setup, &ctxt->layers[i].module_ctxt); /* TODO: errors */

		ctxt->n_layers++;
	}

	if (((compose_setup_t *)setup)->texture.module) {
		const til_module_t	*texture_module;

		texture_module = til_lookup_module(((compose_setup_t *)setup)->texture.module);
		(void) til_module_create_context(texture_module, stream, rand_r(&seed), ticks, n_cpus, s->texture.setup, &ctxt->texture.module_ctxt); /* TODO: errors */
	}

	return &ctxt->til_module_context;
}


static void compose_destroy_context(til_module_context_t *context)
{
	compose_context_t	*ctxt = (compose_context_t *)context;

	for (size_t i = 0; i < ctxt->n_layers; i++)
		til_module_context_free(ctxt->layers[i].module_ctxt);

	til_module_context_free(ctxt->texture.module_ctxt);
	free(ctxt->texture_fb.buf);
	free(context);
}


static void compose_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	compose_context_t	*ctxt = (compose_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr, *texture = &ctxt->texture_fb;
	til_fb_fragment_t	*old_texture = fragment->texture;

	if (ctxt->texture.module_ctxt) {
		if (!ctxt->texture_fb.buf ||
		    ctxt->texture_fb.frame_width != fragment->frame_width ||
		    ctxt->texture_fb.frame_height != fragment->frame_height) {

			ctxt->texture_fb =	(til_fb_fragment_t){
							.buf = realloc(ctxt->texture_fb.buf, fragment->frame_height * fragment->frame_width * sizeof(uint32_t)),

							.frame_width = fragment->frame_width,
							.frame_height = fragment->frame_height,
							.width = fragment->frame_width,
							.height = fragment->frame_height,
							.pitch = fragment->frame_width,
						};
		}

		ctxt->texture_fb.cleared = 0;
		/* XXX: if when snapshotting becomes a thing, ctxt->texture_fb is snapshottable, this will likely break as-is */
		til_module_render(ctxt->texture.module_ctxt, stream, ticks, &texture);
		til_module_render(ctxt->layers[0].module_ctxt, stream, ticks, &fragment);

		for (size_t i = 1; i < ctxt->n_layers; i++) {
			fragment->texture = texture; /* keep forcing our texture, in case something below us installed their own. */
			til_module_render(ctxt->layers[i].module_ctxt, stream, ticks, &fragment);
		}
	} else {
		for (size_t i = 0; i < ctxt->n_layers; i++) {
			fragment->texture = NULL; /* keep forcing no texture, in case something below us installed their own. TODO: more formally define texture semantics as it pertains to module nesting */
			til_module_render(ctxt->layers[i].module_ctxt, stream, ticks, &fragment);
		}
	}

	fragment->texture = old_texture;
	*fragment_ptr = fragment;
}


/* return a randomized valid layers= setting */
static char * compose_random_layers_setting(unsigned seed)
{
	size_t			n_modules, n_rand_overlays, n_overlayable = 0, n_unusable = 0, base_idx;
	char			*layers = NULL;
	const til_module_t	**modules;
	unsigned		unusable_flags = (TIL_MODULE_HERMETIC | TIL_MODULE_EXPERIMENTAL | TIL_MODULE_BUILTIN);

	til_get_modules(&modules, &n_modules);

	for (size_t i = 0; i < n_modules; i++) {
		if ((modules[i]->flags & unusable_flags) ||
		    modules[i] == &compose_module) {
			n_unusable++;

			continue;
		}

		if (modules[i]->flags & TIL_MODULE_OVERLAYABLE)
			n_overlayable++;
	}

	base_idx = rand_r(&seed) % (n_modules - (n_overlayable + n_unusable));
	for (size_t i = 0, j = 0; !layers && i < n_modules; i++) {
		if ((modules[i]->flags & unusable_flags) ||
		    modules[i] == &compose_module)
			continue;

		if (modules[i]->flags & TIL_MODULE_OVERLAYABLE)
			continue;

		if (j++ == base_idx)
			layers = strdup(modules[i]->name);
	}

	/* TODO FIXME: this doesn't prevent duplicate overlays in the random set,
	 * which generally is undesirable - but actually watching the results, is
	 * sometimes interesting.  Maybe another module flag is necessary for indicating
	 * manifold-appropriate overlays.
	 */
	n_rand_overlays = 1 + (rand_r(&seed) % (n_overlayable - 1));
	for (size_t n = 0; n < n_rand_overlays; n++) {
		size_t	rand_idx = rand_r(&seed) % n_overlayable;

		for (size_t i = 0, j = 0; i < n_modules; i++) {
			if ((modules[i]->flags & unusable_flags) ||
			    modules[i] == &compose_module)
				continue;

			if (!(modules[i]->flags & TIL_MODULE_OVERLAYABLE))
				continue;

			if (j++ == rand_idx) {
				char	*new;

				new = realloc(layers, strlen(layers) + 1 + strlen(modules[i]->name) + 1);
				if (!new) {
					free(layers);
					return NULL;
				}

				strcat(new, ",");
				strcat(new, modules[i]->name);
				layers = new;

				break;
			}
		}
	}

	return layers;
}


static void compose_setup_free(til_setup_t *setup)
{
	compose_setup_t	*s = (compose_setup_t *)setup;

	for (size_t i = 0; i < s->n_layers; i++) {
		free(s->layers[i].module);
		til_setup_free(s->layers[i].setup);
	}
	til_setup_free(s->texture.setup);
	free(s->texture.module);
	free(setup);
}


static int compose_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const til_settings_t	*layers_settings, *texture_settings;
	const char		*layers;
	const char		*texture;
	til_setting_t		*texture_module_setting;
	const char		*texture_values[] = {
					"none",
					"blinds",
					"checkers",
					"drizzle",
					"julia",
					"moire",
					"plasma",
					"roto",
					"stars",
					"submit",
					"swab",
					"voronoi",
					NULL
				};
	int			r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Comma-separated list of module layers, in draw-order",
							.key = "layers",
							.preferred = "drizzle,stars,spiro,plato",
							.annotations = NULL,
							.random = compose_random_layers_setting,
							.as_nested_settings = 1,
						},
						&layers, /* XXX: unused in raw-value form, we want the settings instance */
						res_setting,
						res_desc);
	if (r)
		return r;

	/* once layers is described and present, we reach here, and it should have stored its nested settings instance @ res_settings */
	assert(res_setting && *res_setting && (*res_setting)->value_as_nested_settings);
	layers_settings = (*res_setting)->value_as_nested_settings;
	{
		til_setting_t	*layer_setting;

		/* Now that we have the layers value in its own settings instance,
		 * iterate across the settings @ layers_settings, turning each of
		 * those into a nested settings instance as well.
		 * Ultimately turning layers= into an array of unnamed settings
		 * instances (they still get automagically labeled as "layers[N]")
		 */

		/*
		 * Note this relies on til_settings_get_value_by_idx() returning NULL once idx runs off the end,
		 * which is indistinguishable from a NULL-valued setting, so if the user were to fat-finger
		 * an empty layer like "layers=foo,,bar" maybe we'd never reach bar.  This could be made more robust
		 * by explicitly looking at the number of settings and just ignoring NULL values, but maybe
		 * instead we should just prohibit such settings constructions?  Like an empty value should still get
		 * "" not NULL put in it.  FIXME TODO XXX verify/clarify/assert this in code
		 */
		for (size_t i = 0; til_settings_get_value_by_idx(layers_settings, i, &layer_setting); i++) {
			if (!layer_setting->value_as_nested_settings) {
				r = til_setting_desc_new(	layers_settings,
								&(til_setting_spec_t){
									.as_nested_settings = 1,
								}, res_desc);
				if (r < 0)
					return r;

				*res_setting = layer_setting;

				return 1;
			}
		}

		/* At this point, whatever layers were provided have now been turned into a settings
		 * heirarchy.  But haven't yet actually resolved the names of and called down into those
		 * modules' respective setup functions to fully populate the settings as needed.
		 *
		 * Again iterate the layers, but this time resolving module names and calling their setup funcs.
		 * No res_setup is provided here so these will only be building up settings, not producing
		 * baked setups yet.
		 */
		for (size_t i = 0; til_settings_get_value_by_idx(layers_settings, i, &layer_setting); i++) {
			til_setting_t		*layer_module_setting;
			const char		*layer_module_name = til_settings_get_value_by_idx(layer_setting->value_as_nested_settings, 0, &layer_module_setting);
			const til_module_t	*layer_module;

			if (!layer_module_name || !layer_module_setting->desc) {
				r = til_setting_desc_new(	layer_setting->value_as_nested_settings,
								&(til_setting_spec_t){
									.name = "Layer module name",
									.preferred = "none",
									.as_label = 1,
								}, res_desc);
				if (r < 0)
					return r;

				*res_setting = layer_module_name ? layer_module_setting : NULL;

				return 1;
			}

			layer_module = til_lookup_module(layer_module_name);
			if (!layer_module) {
				*res_setting = layer_module_setting;

				return -EINVAL;
			}

			if (layer_module->setup) {
				r = layer_module->setup(layer_setting->value_as_nested_settings, res_setting, res_desc, NULL);
				if (r)
					return r;
			}
		}

		/* at this point all the layers should have all their settings built up in their respective settings instances */
	}

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Module to use for source texture, \"none\" to disable",
							.key = "texture",
							.preferred = texture_values[0],
							.annotations = NULL,
							.values = texture_values,
							.as_nested_settings = 1,
							.as_label = 1,
						},
						&texture,
						res_setting,
						res_desc);
	if (r)
		return r;

	assert(res_setting && *res_setting && (*res_setting)->value_as_nested_settings);
	texture_settings = (*res_setting)->value_as_nested_settings;
	texture = til_settings_get_value_by_idx(texture_settings, 0, &texture_module_setting);

	if (!texture || !texture_module_setting->desc) {
		r = til_setting_desc_new(texture_settings,
					&(til_setting_spec_t){
						/* this is basically just to get the .as_label */
						.name = "Texture module name",
						.preferred = "none",
						.as_label = 1,
					},
					res_desc);
		if (r < 0)
			return r;

		*res_setting = texture ? texture_module_setting : NULL;

		return 1;
	}

	if (strcasecmp(texture, "none")) {
		const til_module_t	*texture_module = til_lookup_module(texture);

		if (!texture_module) {
			*res_setting = texture_module_setting;

			return -EINVAL;
		}

		if (texture_module->setup) {
			r = texture_module->setup(texture_settings, res_setting, res_desc, NULL);
			if (r)
				return r;
		}

		/* now texture settings are complete, but not yet baked (no res_setup) */
	}

	if (res_setup) { /* turn layers settings into an array of compose_setup_layer_t's {name,til_setup_t} */
		size_t			n_layers = til_settings_get_count(layers_settings);
		til_setting_t		*layer_setting;
		compose_setup_t		*setup;

		setup = til_setup_new(settings, sizeof(*setup) + n_layers * sizeof(*setup->layers), compose_setup_free, &compose_module);
		if (!setup)
			return -ENOMEM;

		setup->n_layers = n_layers;

		for (size_t i = 0; til_settings_get_value_by_idx(layers_settings, i, &layer_setting); i++) {
			const char		*layer = til_settings_get_value_by_idx(layer_setting->value_as_nested_settings, 0, NULL);
			const til_module_t	*layer_module = til_lookup_module(layer);

			if (!layer_module) {
				til_setup_free(&setup->til_setup);

				return -EINVAL;
			}

			setup->layers[i].module = strdup(layer);
			if (!setup->layers[i].module) {
				til_setup_free(&setup->til_setup);

				return -ENOMEM;
			}

			r = til_module_setup_finalize(layer_module, layer_setting->value_as_nested_settings, &setup->layers[i].setup);
			if (r < 0) {
				til_setup_free(&setup->til_setup);

				return r;
			}
		}

		if (strcasecmp(texture, "none")) {
			const char		*texture = til_settings_get_value_by_idx(texture_settings, 0, NULL);
			const til_module_t	*texture_module = til_lookup_module(texture);

			if (!texture_module) {
				til_setup_free(&setup->til_setup);

				return -EINVAL;
			}

			setup->texture.module = strdup(texture);
			if (!setup->texture.module) {
				til_setup_free(&setup->til_setup);

				return -ENOMEM;
			}

			r = til_module_setup_finalize(texture_module, texture_settings, &setup->texture.setup);
			if (r < 0) {
				til_setup_free(&setup->til_setup);

				return r;
			}
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}
