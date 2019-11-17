#include <stdlib.h>
#include <time.h>

#include "fb.h"
#include "rototiller.h"
#include "txt/txt.h"
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

#define RTV_SNOW_DURATION_SECS		1
#define RTV_DURATION_SECS		15
#define RTV_CAPTION_DURATION_SECS	5

typedef struct rtv_context_t {
	const rototiller_module_t	**modules;
	size_t				n_modules;

	time_t				next_switch, next_hide_caption;
	const rototiller_module_t	*module, *last_module;
	void				*module_ctxt;
	txt_t				*caption;

	const rototiller_module_t	*snow_module;
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
		ctxt->caption = txt_free(ctxt->caption);
	}

	if (ctxt->module != ctxt->snow_module) {
		ctxt->last_module = ctxt->module;
		ctxt->module = ctxt->snow_module;
		ctxt->next_switch = time(NULL) + RTV_SNOW_DURATION_SECS;
	} else {
		do {
			i = rand() % ctxt->n_modules;
		} while (ctxt->modules[i] == &rtv_module ||
			 ctxt->modules[i] == ctxt->last_module ||
			 ctxt->modules[i] == ctxt->snow_module);

		ctxt->module = ctxt->modules[i];

		ctxt->caption = txt_newf("Title: %s\nAuthor: %s\nDescription: %s\nLicense: %s",
					 ctxt->module->name,
					 ctxt->module->author,
					 ctxt->module->description,
					 ctxt->module->license);

		ctxt->next_switch = time(NULL) + RTV_DURATION_SECS;
		ctxt->next_hide_caption = time(NULL) + RTV_CAPTION_DURATION_SECS;
	}

	if (ctxt->module->create_context)
		ctxt->module_ctxt = ctxt->module->create_context();
}


static void * rtv_create_context(void)
{
	rtv_context_t	*ctxt = calloc(1, sizeof(rtv_context_t));

	ctxt->snow_module = rototiller_lookup_module("snow");
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
	time_t		now = time(NULL);

	if (now >= ctxt->next_switch)
		setup_next_module(ctxt);

	if (now >= ctxt->next_hide_caption)
		ctxt->caption = txt_free(ctxt->caption);

	rototiller_module_render(ctxt->module, ctxt->module_ctxt, fragment);
}


static void rtv_finish_frame(void *context, fb_fragment_t *fragment)
{
	rtv_context_t	*ctxt = context;

	if (!ctxt->caption)
		return;

	txt_render_fragment(ctxt->caption, fragment, 0x00000000,
			    1, fragment->frame_height + 1,
			    (txt_align_t){
					.horiz = TXT_HALIGN_LEFT,
					.vert = TXT_VALIGN_BOTTOM
			    });
	txt_render_fragment(ctxt->caption, fragment, 0xffffffff,
			    0, fragment->frame_height,
			    (txt_align_t){
				.horiz = TXT_HALIGN_LEFT,
				.vert = TXT_VALIGN_BOTTOM
			    });
}
