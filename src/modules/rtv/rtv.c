#include <stdlib.h>
#include <time.h>

#include "fb.h"
#include "rototiller.h"
#include "settings.h"
#include "txt/txt.h"
#include "util.h"

/* Copyright (C) 2019 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements an MTV-inspired random slideshow of rototiller modules.
 *
 * Eventually it'd be nice to have it show a caption every time a new
 * module starts overlaying the name, author, license, etc.
 *
 * Some TODO items:
 * - optionally persist module contexts so they resume rather than restart
 * - runtime-configurable duration
 */

#define RTV_SNOW_DURATION_SECS		1
#define RTV_DURATION_SECS		15
#define RTV_CAPTION_DURATION_SECS	5

typedef struct rtv_module_t {
	const rototiller_module_t	*module;
	unsigned			ticket;
} rtv_module_t;

typedef struct rtv_context_t {
	unsigned			n_cpus;

	time_t				next_switch, next_hide_caption;
	const rototiller_module_t	*module, *last_module;
	void				*module_ctxt;
	txt_t				*caption;

	const rototiller_module_t	*snow_module;

	size_t				n_modules;
	rtv_module_t			modules[];
} rtv_context_t;

static void setup_next_module(rtv_context_t *ctxt);
static void * rtv_create_context(unsigned num_cpus);
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


static int cmp_modules(const void *p1, const void *p2)
{
	const rtv_module_t	*m1 = p1, *m2 = p2;

	if (m1->ticket < m2->ticket)
		return -1;

	if (m1->ticket > m2->ticket)
		return 1;

	return 0;
}


static void randomize_modules(rtv_context_t *ctxt)
{
	for (size_t i = 0; i < ctxt->n_modules; i++)
		ctxt->modules[i].ticket = rand();

	qsort(ctxt->modules, ctxt->n_modules, sizeof(rtv_module_t), cmp_modules);
}


static char * randomize_module_setup(const rototiller_module_t *module)
{
	settings_t	*settings;
	setting_desc_t	*desc;
	char		*arg;

	if (!module->setup)
		return NULL;

	settings = settings_new(NULL);
	if (!settings)
		return NULL;

	while (module->setup(settings, &desc) > 0) {
		if (desc->random) {
			char	*value;

			value = desc->random();
			settings_add_value(settings, desc->key, value);
			free(value);
		} else if (desc->values) {
			int	n;

			for (n = 0; desc->values[n]; n++);

			n = rand() % n;

			settings_add_value(settings, desc->key, desc->values[n]);
		} else {
			settings_add_value(settings, desc->key, desc->preferred);
		}

		setting_desc_free(desc);
	}

	arg = settings_as_arg(settings);
	settings_free(settings);

	return arg;
}


static void setup_next_module(rtv_context_t *ctxt)
{
	time_t	now = time(NULL);

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
		ctxt->next_switch = now + RTV_SNOW_DURATION_SECS;
	} else {
		char	*setup;
		size_t	i;

		for (i = 0; i < ctxt->n_modules; i++) {
			if (ctxt->modules[i].module == ctxt->last_module) {
				i++;
				break;
			}
		}

		if (i >= ctxt->n_modules) {
			randomize_modules(ctxt);
			ctxt->last_module = NULL;
			i = 0;
		}

		ctxt->module = ctxt->modules[i].module;

		setup = randomize_module_setup(ctxt->module);

		ctxt->caption = txt_newf("Title: %s\nAuthor: %s\nDescription: %s\nLicense: %s%s%s",
					 ctxt->module->name,
					 ctxt->module->author,
					 ctxt->module->description,
					 ctxt->module->license,
					 setup ? "\nSettings: " : "",
					 setup ? setup : "");

		free(setup);

		ctxt->next_switch = now + RTV_DURATION_SECS;
		ctxt->next_hide_caption = now + RTV_CAPTION_DURATION_SECS;
	}

	if (ctxt->module->create_context)
		ctxt->module_ctxt = ctxt->module->create_context(ctxt->n_cpus);
}


static void * rtv_create_context(unsigned num_cpus)
{
	rtv_context_t			*ctxt;
	const rototiller_module_t	**modules;
	size_t				n_modules;

	rototiller_get_modules(&modules, &n_modules);

	ctxt = calloc(1, sizeof(rtv_context_t) + n_modules * sizeof(rtv_module_t));
	if (!ctxt)
		return NULL;

	ctxt->n_cpus = num_cpus;
	ctxt->snow_module = rototiller_lookup_module("snow");

	for (size_t i = 0; i < n_modules; i++) {
		if (modules[i] == &rtv_module ||
		    modules[i] == ctxt->snow_module)
			continue;

		ctxt->modules[ctxt->n_modules++].module = modules[i];
	}

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
