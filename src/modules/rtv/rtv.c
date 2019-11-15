#include <stdlib.h>
#include <time.h>

#include "fb.h"
#include "rototiller.h"
#include "util.h"

/* Copyright (C) 2019 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements an MTV-inspired random slideshow of rototiller modules.
 *
 * Eventually it'd be nice to have it show a caption every time a new
 * module starts overlaying the name, author, license, etc.
 *
 * Some TODO items:
 * - show captions (need text rendering)
 * - optionally persist module contexts so they resume rather than restart
 * - runtime-configurable duration
 * - randomize runtime settings of shown modules
 * - redo the next module selection from random to
 *   walking the list and randomizing the list every
 *   time it completes a cycle.   The current dumb
 *   random technique will happily keep showing you the
 *   same thing over and over.
 */

#define RTV_DURATION_SECS	15

typedef struct rtv_context_t {
	const rototiller_module_t	**modules;
	size_t				n_modules;

	time_t				last_switch;
	const rototiller_module_t	*module;
	void				*module_ctxt;
} rtv_context_t;

static void setup_next_module(rtv_context_t *ctxt);
static void * rtv_create_context(void);
static void rtv_destroy_context(void *context);
static void rtv_prepare_frame(void *context, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter);
static void rtv_finish_frame(void *context, fb_fragment_t *fragment);


rototiller_module_t	rtv_module = {
	.create_context = rtv_create_context,
	.destroy_context = rtv_destroy_context,
	.prepare_frame = rtv_prepare_frame,
	.finish_frame = rtv_finish_frame,
	.name = "rtv",
	.description = "Rototiller TV",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};


static void setup_next_module(rtv_context_t *ctxt)
{
	int	i;

	/* TODO: most of this module stuff should probably be
	 * in rototiller.c helpers, but it's harmless for now.
	 */
	if (ctxt->module) {
		if (ctxt->module->destroy_context)
			ctxt->module->destroy_context(ctxt->module_ctxt);

		ctxt->module_ctxt = NULL;
	}

	do {
		i = rand() % ctxt->n_modules;
	} while (ctxt->modules[i] == &rtv_module || ctxt->modules[i] == ctxt->module);

	ctxt->module = ctxt->modules[i];
	if (ctxt->module->create_context)
		ctxt->module_ctxt = ctxt->module->create_context();

	ctxt->last_switch = time(NULL);
}


static void * rtv_create_context(void)
{
	rtv_context_t	*ctxt = calloc(1, sizeof(rtv_context_t));

	rototiller_get_modules(&ctxt->modules, &ctxt->n_modules);
	setup_next_module(ctxt);

	return ctxt;
}


static void rtv_destroy_context(void *context)
{
	free(context);
}


static void rtv_prepare_frame(void *context, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter)
{
	rtv_context_t	*ctxt = context;

	if (!ctxt->last_switch || time(NULL) - ctxt->last_switch > RTV_DURATION_SECS)
		setup_next_module(ctxt);

	rototiller_module_render(ctxt->module, ctxt->module_ctxt, fragment);
}


static void rtv_finish_frame(void *context, fb_fragment_t *fragment)
{
	/* TODO: this is stubbed here for drawing the caption overlay */
}
