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

#define COMPOSE_DEFAULT_LAYER_MODULE	"moire"
#define COMPOSE_DEFAULT_TEXTURE_MODULE	"none"

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
	til_setup_t		*module_setup;
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

		layer_module = ((compose_setup_t *)setup)->layers[i].module_setup->creator;
		(void) til_module_create_context(layer_module, stream, rand_r(&seed), ticks, n_cpus, s->layers[i].module_setup, &ctxt->layers[i].module_ctxt); /* TODO: errors */

		ctxt->n_layers++;
	}

	if (((compose_setup_t *)setup)->texture.module_setup) {
		const til_module_t	*texture_module;

		texture_module = ((compose_setup_t *)setup)->texture.module_setup->creator;
		(void) til_module_create_context(texture_module, stream, rand_r(&seed), ticks, n_cpus, s->texture.module_setup, &ctxt->texture.module_ctxt); /* TODO: errors */
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

	for (size_t i = 0; i < s->n_layers; i++)
		til_setup_free(s->layers[i].module_setup);

	til_setup_free(s->texture.module_setup);
	free(setup);
}


static int compose_layer_module_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*exclusions[] = { "none", "compose" /* XXX: prevent infinite recursion */, NULL };

	/* nested compose might be interesting, but there needs to be guards to prevent the potential infinite recursion.
	 * note you can still access it via the ':' override prefix
	 */

	return til_module_setup_full(settings,
				     res_setting,
				     res_desc,
				     res_setup,
				     "Layer module name",
				     COMPOSE_DEFAULT_LAYER_MODULE,
				     (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC),
				     exclusions);
}


static int compose_texture_module_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	return til_module_setup_full(settings,
				     res_setting,
				     res_desc,
				     res_setup,
				     "Texture module name",
				     COMPOSE_DEFAULT_TEXTURE_MODULE,
				     (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC),
				     NULL);
}


static int compose_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const til_settings_t	*layers_settings, *texture_settings;
	til_setting_t		*layers;
	til_setting_t		*texture;
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

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Comma-separated list of module layers, in draw-order",
							.key = "layers",
							.preferred = "drizzle,stars,spiro,plato",
							.annotations = NULL,
							/* TODO: .values = could have a selection of interesting preset compositions... */
							.random = compose_random_layers_setting,
							.as_nested_settings = 1,
						},
						&layers, /* XXX: unused in raw-value form, we want the settings instance */
						res_setting,
						res_desc);
	if (r)
		return r;

	layers_settings = layers->value_as_nested_settings;
	assert(layers_settings);
	{
		til_setting_t	*layer_setting;

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

		for (size_t i = 0; til_settings_get_value_by_idx(layers_settings, i, &layer_setting); i++) {
			r = compose_layer_module_setup(layer_setting->value_as_nested_settings,
						       res_setting,
						       res_desc,
						       NULL); /* XXX: note no res_setup, must defer finalize */
			if (r)
				return r;
		}
	}

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Module to use for source texture, \"none\" to disable",
							.key = "texture",
							.preferred = COMPOSE_DEFAULT_TEXTURE_MODULE,
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

	r = compose_texture_module_setup(texture_settings,
					 res_setting,
					 res_desc,
					 NULL); /* XXX: note no res_setup, must defer finalize */
	if (r)
		return r;

	if (res_setup) { /* turn layers settings into an array of compose_setup_layer_t's {name,til_setup_t} */
		size_t		n_layers = til_settings_get_count(layers_settings);
		til_setting_t	*layer_setting;
		compose_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup) + n_layers * sizeof(*setup->layers), compose_setup_free, &compose_module);
		if (!setup)
			return -ENOMEM;

		setup->n_layers = n_layers;

		for (size_t i = 0; til_settings_get_value_by_idx(layers_settings, i, &layer_setting); i++) {
			r = compose_layer_module_setup(layer_setting->value_as_nested_settings,
						       res_setting,
						       res_desc,
						       &setup->layers[i].module_setup); /* finalize! */
			if (r < 0) {
				til_setup_free(&setup->til_setup);
				return r;
			}

			assert(r == 0);
		}

		r = compose_texture_module_setup(texture_settings,
						 res_setting,
						 res_desc,
						 &setup->texture.module_setup); /* finalize! */
		if (r < 0) {
			til_setup_free(&setup->til_setup);
			return r;
		}

		assert(r == 0);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
