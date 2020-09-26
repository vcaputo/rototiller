#include <stdlib.h>
#include <time.h>

#include "fb.h"
#include "rototiller.h"
#include "settings.h"
#include "txt/txt.h"
#include "util.h"

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
	const rototiller_module_t	*module;
	void				*module_ctxt;
	char				*settings;
} compose_layer_t;

typedef struct compose_context_t {
	unsigned			n_cpus;

	size_t				n_layers;
	compose_layer_t			layers[];
} compose_context_t;

static void * compose_create_context(unsigned ticks, unsigned num_cpus);
static void compose_destroy_context(void *context);
static void compose_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter);
static int compose_setup(const settings_t *settings, setting_desc_t **next_setting);

static char	**compose_layers;


rototiller_module_t	compose_module = {
	.create_context = compose_create_context,
	.destroy_context = compose_destroy_context,
	.prepare_frame = compose_prepare_frame,
	.name = "compose",
	.description = "Layered Modules Compositor",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
	.setup = compose_setup,
};


static void * compose_create_context(unsigned ticks, unsigned num_cpus)
{
	compose_context_t	*ctxt;
	int			n;

	for (n = 0; compose_layers[n]; n++);

	ctxt = calloc(1, sizeof(compose_context_t) + n * sizeof(compose_layer_t));
	if (!ctxt)
		return NULL;

	ctxt->n_cpus = num_cpus;

	for (int i = 0; i < n; i++) {
		const rototiller_module_t	*module;

		module = rototiller_lookup_module(compose_layers[i]);

		ctxt->layers[i].module = module;
		if (module->create_context)
			ctxt->layers[i].module_ctxt = module->create_context(ticks, num_cpus);

		ctxt->n_layers++;
	}

	return ctxt;
}


static void compose_destroy_context(void *context)
{
	compose_context_t	*ctxt = context;

	for (int i = 0; i < ctxt->n_layers; i++) {
		if (ctxt->layers[i].module_ctxt)
			ctxt->layers[i].module->destroy_context(ctxt->layers[i].module_ctxt);
	}
	free(context);
}


static void compose_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter)
{
	compose_context_t	*ctxt = context;

	fb_fragment_zero(fragment);

	for (int i = 0; i < ctxt->n_layers; i++)
		rototiller_module_render(ctxt->layers[i].module, ctxt->layers[i].module_ctxt, ticks, fragment);
}


static int compose_setup(const settings_t *settings, setting_desc_t **next_setting)
{
	const char	*layers;

	layers = settings_get_value(settings, "layers");
	if (!layers) {
		int	r;

		r = setting_desc_clone(&(setting_desc_t){
						.name = "Colon-Separated List Of Module Layers, In Draw Order",
						.key = "layers",
						.preferred = "drizzle:stars:spiro",
						.annotations = NULL
					}, next_setting);
		if (r < 0)
			return r;

		return 1;
	}

	/* turn layers colon-separated list into a null-terminated array of strings */
	{
		const rototiller_module_t	**modules;
		size_t				n_modules;
		char				*toklayers, *layer;
		int				n = 2;

		rototiller_get_modules(&modules, &n_modules);

		toklayers = strdup(layers);
		if (!toklayers)
			return -ENOMEM;

		layer = strtok(toklayers, ":");
		do {
			char	**new;
			size_t	i;

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

			new = realloc(compose_layers, n * sizeof(*compose_layers));
			if (!new)
				return -ENOMEM;

			new[n - 2] = layer;
			new[n - 1] = NULL;
			n++;

			compose_layers = new;
		} while (layer = strtok(NULL, ":"));
	}

	return 0;
}
