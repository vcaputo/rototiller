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
	.description = "Rototiller montage (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};


static void * montage_create_context(unsigned num_cpus)
{
	const rototiller_module_t	**modules, *rtv_module, *pixbounce_module, *stars_module;
	size_t				n_modules;
	montage_context_t		*ctxt;

	ctxt = calloc(1, sizeof(montage_context_t));
	if (!ctxt)
		return NULL;

	rototiller_get_modules(&modules, &n_modules);

	ctxt->modules = calloc(n_modules, sizeof(rototiller_module_t *));
	if (!ctxt->modules) {
		free(ctxt);

		return NULL;
	}

	rtv_module = rototiller_lookup_module("rtv");
	pixbounce_module = rototiller_lookup_module("pixbounce");
	stars_module = rototiller_lookup_module("stars");

	for (size_t i = 0; i < n_modules; i++) {
		const rototiller_module_t	*module = modules[i];

		if (module == &montage_module ||	/* prevents recursion */
		    module == rtv_module )		/* also prevents recursion, rtv can run montage */
			continue;

		ctxt->modules[ctxt->n_modules++] = module;
	}

	ctxt->n_cpus = num_cpus;

	ctxt->contexts = calloc(ctxt->n_modules, sizeof(void *));
	if (!ctxt->contexts) {
		free(ctxt);

		return NULL;
	}

	for (size_t i = 0; i < ctxt->n_modules; i++) {
		const rototiller_module_t	*module = ctxt->modules[i];

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

		if (!ctxt->contexts[i])
			continue;

		 module->destroy_context(ctxt->contexts[i]);
	}

	free(ctxt->contexts);
	free(ctxt->modules);
	free(ctxt);
}



/* this is a hacked up derivative of fb_fragment_tile_single() */
static int montage_fragment_tile(const fb_fragment_t *fragment, unsigned tile_width, unsigned tile_height, unsigned number, fb_fragment_t *res_fragment)
{
	unsigned	w = fragment->width / tile_width, h = fragment->height / tile_height;
	unsigned	x, y, xoff, yoff;

#if 0
	/* total coverage isn't important in montage, leave blank gaps */
	/* I'm keeping this here for posterity though and to record a TODO:
	 * it might be desirable to try center the montage when there must be gaps,
	 * rather than letting the gaps always fall on the far side.
	 */
	if (w * tile_width < fragment->width)
		w++;

	if (h * tile_height < fragment->height)
		h++;
#endif

	y = number / w;
	if (y >= h)
		return 0;

	x = number - (y * w);

	xoff = x * tile_width;
	yoff = y * tile_height;

	res_fragment->buf = (void *)fragment->buf + (yoff * fragment->pitch) + (xoff * 4);
	res_fragment->x = 0;					/* fragment is a new frame */
	res_fragment->y = 0;					/* fragment is a new frame */
	res_fragment->width = MIN(fragment->width - xoff, tile_width);
	res_fragment->height = MIN(fragment->height - yoff, tile_height);
	res_fragment->frame_width = res_fragment->width;	/* fragment is a new frame */
	res_fragment->frame_height = res_fragment->height;	/* fragment is a new frame */
	res_fragment->stride = fragment->stride + ((fragment->width - res_fragment->width) * 4);
	res_fragment->pitch = fragment->pitch;
	res_fragment->number = number;

	return 1;
}


/* The fragmenter in montage is serving double-duty:
 * 1. it divides the frame into subfragments for threaded rendering
 * 2. it determines which modules will be rendered where via fragment->number
 */
static int montage_fragmenter(void *context, const fb_fragment_t *fragment, unsigned number, fb_fragment_t *res_fragment)
{
	montage_context_t	*ctxt = context;
	float			root = sqrtf(ctxt->n_modules);
	unsigned		tile_width = fragment->frame_width / ceilf(root);	/* screens are wide, give excess to the width */
	unsigned		tile_height = fragment->frame_height / floorf(root);	/* take from the height */
	int			ret;

	/* XXX: this could all be more clever and make some tiles bigger than others to deal with fractional square roots,
	 * but this is good enough for now considering the simplicity.
	 */
	ret = montage_fragment_tile(fragment, tile_width, tile_height, number, res_fragment);
	if (!ret)
		return 0;

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

	if (fragment->number >= ctxt->n_modules) {
		fb_fragment_zero(fragment);

		return;
	}


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

