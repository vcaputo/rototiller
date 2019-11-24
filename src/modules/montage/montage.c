#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "fb.h"
#include "rototiller.h"
#include "settings.h"
#include "util.h"

/* Copyright (C) 2019 - Vito Caputo <vcaputo@pengaru.com> */

typedef struct montage_context_t {
	const rototiller_module_t	**modules;
	const rototiller_module_t	*rtv_module;
	void				**contexts;
	size_t				n_modules;
	unsigned			n_cpus;
} montage_context_t;

static void setup_next_module(montage_context_t *ctxt);
static void * montage_create_context(unsigned num_cpus);
static void montage_destroy_context(void *context);
static void montage_prepare_frame(void *context, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter);
static void montage_render_fragment(void *context, unsigned cpu, fb_fragment_t *fragment);


rototiller_module_t	montage_module = {
	.create_context = montage_create_context,
	.destroy_context = montage_destroy_context,
	.prepare_frame = montage_prepare_frame,
	.render_fragment = montage_render_fragment,
	.name = "montage",
	.description = "Rototiller montage",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};


static int skip_module(montage_context_t *ctxt, const rototiller_module_t *module)
{
	/* prevent recursion */
	if (module == &montage_module)
		return 1;

	/* also prevents recursion, as rtv could run montage... */
	if (module == ctxt->rtv_module)
		return 1;

	return 0;
}


static void * montage_create_context(unsigned num_cpus)
{
	montage_context_t	*ctxt = calloc(1, sizeof(montage_context_t));

	ctxt->n_cpus = num_cpus;
	rototiller_get_modules(&ctxt->modules, &ctxt->n_modules);

	ctxt->contexts = calloc(ctxt->n_modules, sizeof(void *));
	if (!ctxt->contexts) {
		free(ctxt);

		return NULL;
	}

	ctxt->rtv_module = rototiller_lookup_module("rtv");

	for (int i = 0; i < ctxt->n_modules; i++) {
		const rototiller_module_t	*module = ctxt->modules[i];

		if (skip_module(ctxt, module))
			continue;

		if (module->create_context)	/* FIXME errors */
			ctxt->contexts[i] = module->create_context(num_cpus);
	}

	return ctxt;
}


static void montage_destroy_context(void *context)
{
	montage_context_t	*ctxt = context;

	for (int i = 0; i < ctxt->n_modules; i++) {
		const rototiller_module_t	*module = ctxt->modules[i];

		if (skip_module(ctxt, module))
			continue;

		if (!ctxt->contexts[i])
			continue;

		 module->destroy_context(ctxt->contexts[i]);
	}

	free(context);
}


/* The fragmenter in montage is serving double-duty:
 * 1. it divides the frame into subfragments for threaded rendering
 * 2. it determines which modules will be rendered where via fragment->number
 */
static int montage_fragmenter(void *context, const fb_fragment_t *fragment, unsigned number, fb_fragment_t *res_fragment)
{
	montage_context_t	*ctxt = context;
	float			root = sqrtf((float)ctxt->n_modules);
	int			ret;

	ret = fb_fragment_tile_single(fragment, fragment->frame_height / root, number, res_fragment);
	if (!ret)
		return 0;

	if (number >= ctxt->n_modules)
		return 0;

	/* as these tiles are frames of their own rather than subfragments, override these values */
	res_fragment->x = res_fragment->y = 0;
	res_fragment->frame_width = res_fragment->width;
	res_fragment->frame_height = res_fragment->height;

	return ret;
}



static void montage_prepare_frame(void *context, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter)
{
	montage_context_t	*ctxt = context;

	*res_fragmenter = montage_fragmenter;
}


static void montage_render_fragment(void *context, unsigned cpu, fb_fragment_t *fragment)
{
	montage_context_t		*ctxt = context;
	const rototiller_module_t	*module = ctxt->modules[fragment->number];

	if (skip_module(ctxt, module))
		return;

	/* since we're *already* in a threaded render of tiles, no further
	 * threading within the montage tiles is desirable, so the per-module
	 * render is done explicitly serially here in an open-coded ad-hoc
	 * fashion for now. FIXME TODO: move this into rototiller.c
	 */
	if (module->prepare_frame) {
		rototiller_fragmenter_t	unused;

		/* XXX FIXME: ignoring the fragmenter here is a violation of the module API,
		 * rototiller.c should have a module render interface with explicit non-threading
		 * that still does all the necessary fragmenting as needed.
		 *
		 * Today, I can get away with this, because montage is the only module that's
		 * sensitive to this aspect of the API and it skips itself.
		 */

		module->prepare_frame(ctxt->contexts[fragment->number], 1, fragment, &unused);
	}

	if (module->render_fragment)
		module->render_fragment(ctxt->contexts[fragment->number], 0, fragment);
}

