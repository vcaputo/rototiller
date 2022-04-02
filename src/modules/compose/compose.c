#include <stdlib.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"
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
	void			*module_ctxt;
	char			*settings;
} compose_layer_t;

typedef struct compose_context_t {
	unsigned		n_cpus;

	size_t			n_layers;
	compose_layer_t		layers[];
} compose_context_t;

typedef struct compose_setup_t {
	size_t			n_layers;
	char			*layers[];
} compose_setup_t;

static void * compose_create_context(unsigned ticks, unsigned num_cpus, void *setup);
static void compose_destroy_context(void *context);
static void compose_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter);
static int compose_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, void **res_setup);

static compose_setup_t compose_default_setup = {
	.layers = { "drizzle", "stars", "spiro", "plato", NULL },
};


til_module_t	compose_module = {
	.create_context = compose_create_context,
	.destroy_context = compose_destroy_context,
	.prepare_frame = compose_prepare_frame,
	.name = "compose",
	.description = "Layered modules compositor",
	.setup = compose_setup,
};


static void * compose_create_context(unsigned ticks, unsigned num_cpus, void *setup)
{
	compose_context_t	*ctxt;
	size_t			n;

	if (!setup)
		setup = &compose_default_setup;

	for (n = 0; ((compose_setup_t *)setup)->layers[n]; n++);

	ctxt = calloc(1, sizeof(compose_context_t) + n * sizeof(compose_layer_t));
	if (!ctxt)
		return NULL;

	ctxt->n_cpus = num_cpus;

	for (int i = 0; i < n; i++) {
		const til_module_t	*module;

		module = til_lookup_module(((compose_setup_t *)setup)->layers[i]);

		ctxt->layers[i].module = module;
		(void) til_module_create_context(module, ticks, NULL, &ctxt->layers[i].module_ctxt);

		ctxt->n_layers++;
	}

	return ctxt;
}


static void compose_destroy_context(void *context)
{
	compose_context_t	*ctxt = context;

	for (int i = 0; i < ctxt->n_layers; i++)
		til_module_destroy_context(ctxt->layers[i].module, ctxt->layers[i].module_ctxt);
	free(context);
}


static void compose_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter)
{
	compose_context_t	*ctxt = context;

	til_fb_fragment_zero(fragment);

	for (int i = 0; i < ctxt->n_layers; i++)
		til_module_render(ctxt->layers[i].module, ctxt->layers[i].module_ctxt, ticks, fragment);
}


static int compose_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, void **res_setup)
{
	const char	*layers;
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Colon-separated list of module layers, in draw-order",
							.key = "layers",
							.preferred = "drizzle:stars:spiro:plato",
							.annotations = NULL
						},
						&layers,
						res_setting,
						res_desc);
	if (r)
		return r;

	/* turn layers colon-separated list into a null-terminated array of strings */
	if (res_setup) {
		compose_setup_t		*setup = NULL;
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

		do {
			compose_setup_t	*new;
			size_t		i;

			/* other meta-modules like montage and rtv may need to
			 * have some consideration here, but for now I'm just
			 * going to let the user potentially compose with montage
			 * or rtv as one of the layers.
			 */
			if (!strcmp(layer, "compose")) /* XXX: prevent infinite recursion */
				return -EINVAL;

			for (i = 0; i < n_modules; i++) {
				if (!strcmp(layer, modules[i]->name))
					break;
			}

			if (i >= n_modules)
				return -EINVAL;

			new = realloc(setup, sizeof(*setup) + n * sizeof(*setup->layers));
			if (!new)
				return -ENOMEM;

			new->layers[n - 2] = layer;
			new->layers[n - 1] = NULL;
			n++;

			setup = new;
		} while (layer = strtok(NULL, ":"));

		*res_setup = setup;
	}

	return 0;
}
