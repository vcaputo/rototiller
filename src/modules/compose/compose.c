#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_util.h"

#include "txt/txt.h"

/* Copyright (C) 2020 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements a rudimentary compositing module for layering
 * the output from other modules into a single frame.
 */

/* some TODOs:
 * - support randomizing settings and context resets, configurable
 * - maybe add a way for the user to supply the settings on the cli
 *   for the composed layers.  That might actually need to be a more
 *   general solution in the top-level rototiller code, since the
 *   other meta modules like montage and rtv could probably benefit
 *   from the ability to feed in settings to the underlying modules.
 */

typedef struct compose_layer_t {
	const til_module_t	*module;
	til_module_context_t	*module_ctxt;
	char			*settings;
} compose_layer_t;

typedef struct compose_context_t {
	til_module_context_t	til_module_context;

	til_fb_fragment_t	texture_fb;
	compose_layer_t		texture;
	size_t			n_layers;
	compose_layer_t		layers[];
} compose_context_t;

typedef struct compose_setup_t {
	til_setup_t		til_setup;
	char			*texture;
	size_t			n_layers;
	char			*layers[];
} compose_setup_t;

static til_module_context_t * compose_create_context(unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup);
static void compose_destroy_context(til_module_context_t *context);
static void compose_render_fragment(til_module_context_t *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment);
static int compose_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);

static compose_setup_t compose_default_setup = {
	.layers = { "drizzle", "stars", "spiro", "plato", NULL },
};


til_module_t	compose_module = {
	.create_context = compose_create_context,
	.destroy_context = compose_destroy_context,
	.render_fragment = compose_render_fragment,
	.name = "compose",
	.description = "Layered modules compositor",
	.setup = compose_setup,
};


static til_module_context_t * compose_create_context(unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	compose_context_t	*ctxt;
	size_t			n;

	if (!setup)
		setup = &compose_default_setup.til_setup;

	for (n = 0; ((compose_setup_t *)setup)->layers[n]; n++);

	ctxt = til_module_context_new(sizeof(compose_context_t) + n * sizeof(compose_layer_t), seed, ticks, n_cpus);
	if (!ctxt)
		return NULL;

	for (size_t i = 0; i < n; i++) {
		const til_module_t	*layer_module;
		til_setup_t		*layer_setup = NULL;

		layer_module = til_lookup_module(((compose_setup_t *)setup)->layers[i]);
		(void) til_module_randomize_setup(layer_module, rand_r(&seed), &layer_setup, NULL);

		ctxt->layers[i].module = layer_module;
		(void) til_module_create_context(layer_module, rand_r(&seed), ticks, 0, layer_setup, &ctxt->layers[i].module_ctxt);
		til_setup_free(layer_setup);

		ctxt->n_layers++;
	}

	if (((compose_setup_t *)setup)->texture) {
		til_setup_t	*texture_setup = NULL;

		ctxt->texture.module = til_lookup_module(((compose_setup_t *)setup)->texture);
		(void) til_module_randomize_setup(ctxt->texture.module, rand_r(&seed), &texture_setup, NULL);

		(void) til_module_create_context(ctxt->texture.module, rand_r(&seed), ticks, 0, texture_setup, &ctxt->texture.module_ctxt);
		til_setup_free(texture_setup);
	}

	return &ctxt->til_module_context;
}


static void compose_destroy_context(til_module_context_t *context)
{
	compose_context_t	*ctxt = (compose_context_t *)context;

	for (int i = 0; i < ctxt->n_layers; i++)
		til_module_context_free(ctxt->layers[i].module_ctxt);

	if (ctxt->texture.module)
		til_module_context_free(ctxt->texture.module_ctxt);

	free(ctxt->texture_fb.buf);

	free(context);
}


static void compose_render_fragment(til_module_context_t *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	compose_context_t	*ctxt = (compose_context_t *)context;

	if (ctxt->texture.module) {
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
		til_module_render(ctxt->texture.module_ctxt, ticks, &ctxt->texture_fb);

		til_module_render(ctxt->layers[0].module_ctxt, ticks, fragment);

		for (size_t i = 1; i < ctxt->n_layers; i++) {
			til_fb_fragment_t	textured = *fragment;

			textured.texture = &ctxt->texture_fb;

			til_module_render(ctxt->layers[i].module_ctxt, ticks, &textured);
		}
	} else {
		for (size_t i = 0; i < ctxt->n_layers; i++)
			til_module_render(ctxt->layers[i].module_ctxt, ticks, fragment);
	}
}


/* return a randomized valid layers= setting */
static char * compose_random_layers_setting(unsigned seed)
{
	size_t			n_modules, n_rand_overlays, n_overlayable = 0, base_idx;
	char			*layers = NULL;
	const til_module_t	**modules;

	til_get_modules(&modules, &n_modules);

	for (size_t i = 0; i < n_modules; i++) {
		if (modules[i]->flags & TIL_MODULE_OVERLAYABLE)
			n_overlayable++;
	}

	base_idx = rand_r(&seed) % (n_modules - n_overlayable);
	for (size_t i = 0, j = 0; !layers && i < n_modules; i++) {
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
			if (!(modules[i]->flags & TIL_MODULE_OVERLAYABLE))
				continue;

			if (j++ == rand_idx) {
				char	*new;

				new = realloc(layers, strlen(layers) + 1 + strlen(modules[i]->name) + 1);
				if (!new) {
					free(layers);
					return NULL;
				}

				strcat(new, ":");
				strcat(new, modules[i]->name);
				layers = new;

				break;
			}
		}
	}

	return layers;
}


static int compose_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*layers;
	const char	*texture;
	const char	*texture_values[] = {
				"none",
				"blinds",
				"checkers",
				"drizzle",
				"julia",
				"plasma",
				"roto",
				"stars",
				"submit",
				"swab",
				"voronoi",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Colon-separated list of module layers, in draw-order",
							.key = "layers",
							.preferred = "drizzle:stars:spiro:plato",
							.annotations = NULL,
							.random = compose_random_layers_setting,
						},
						&layers,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Module to use for source texture, \"none\" to disable",
							.key = "texture",
							.preferred = texture_values[0],
							.annotations = NULL,
							.values = texture_values,
						},
						&texture,
						res_setting,
						res_desc);
	if (r)
		return r;

	/* turn layers colon-separated list into a null-terminated array of strings */
	if (res_setup) {
		compose_setup_t		*setup;
		const til_module_t	**modules;
		size_t			n_modules;
		char			*toklayers, *layer;
		int			n = 2;

		til_get_modules(&modules, &n_modules);

		toklayers = strdup(layers);
		if (!toklayers)
			return -ENOMEM;

		layer = strtok(toklayers, ":");
		if (!layer)
			return -EINVAL;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		do {
			compose_setup_t	*new;
			size_t		i;

			/* other meta-modules like montage and rtv may need to
			 * have some consideration here, but for now I'm just
			 * going to let the user potentially compose with montage
			 * or rtv as one of the layers.
			 */
			if (!strcasecmp(layer, "compose")) { /* XXX: prevent infinite recursion */
				til_setup_free(&setup->til_setup);

				return -EINVAL;
			}

			for (i = 0; i < n_modules; i++) {
				if (!strcasecmp(layer, modules[i]->name))
					break;
			}

			if (i >= n_modules) {
				til_setup_free(&setup->til_setup);

				return -EINVAL;
			}

			new = realloc(setup, sizeof(*setup) + n * sizeof(*setup->layers));
			if (!new) {
				til_setup_free(&setup->til_setup);

				return -ENOMEM;
			}

			new->layers[n - 2] = layer;
			new->layers[n - 1] = NULL;
			n++;

			setup = new;
		} while ((layer = strtok(NULL, ":")));

		if (strcasecmp(texture, "none")) {
			const til_module_t	*texture_module;

			texture_module = til_lookup_module(texture);
			if (!texture_module) {
				til_setup_free(&setup->til_setup);

				return -EINVAL;
			}

			setup->texture = strdup(texture);
			if (!setup->texture) {
				til_setup_free(&setup->til_setup);

				return -ENOMEM;
			}
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}
