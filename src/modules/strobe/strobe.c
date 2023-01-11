#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

/* Dead-simple strobe light, initially made to try simulate this contraption:
 * https://en.wikipedia.org/wiki/Dreamachine
 *
 * But it might actually have some general utility in compositing.
 */

/* Copyright (C) 2022 Vito Caputo <vcaputo@pengaru.com> */

/* TODO:
 *	- Make period setting more flexible
 */

#define	STROBE_DEFAULT_PERIOD		.1

typedef struct strobe_setup_t {
	til_setup_t		til_setup;
	float			period;
} strobe_setup_t;

typedef struct strobe_context_t {
	til_module_context_t	til_module_context;
	strobe_setup_t		setup;
	unsigned		ticks;
	unsigned		flash:1;
	unsigned		flash_ready:1;
} strobe_context_t;


static strobe_setup_t strobe_default_setup = {
	.period = STROBE_DEFAULT_PERIOD,
};


static til_module_context_t * strobe_create_context(til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	strobe_context_t	*ctxt;

	if (!setup)
		setup = &strobe_default_setup.til_setup;

	ctxt = til_module_context_new(stream, sizeof(strobe_context_t), seed, ticks, n_cpus, path);
	if (!ctxt)
		return NULL;

	ctxt->setup = *(strobe_setup_t *)setup;
	ctxt->ticks = ticks;

	return &ctxt->til_module_context;
}


static void strobe_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	strobe_context_t	*ctxt = (strobe_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu };

	if (ctxt->flash_ready && (ticks - ctxt->ticks >= (unsigned)(ctxt->setup.period * 1000.f))){
		ctxt->flash = 1;
		ctxt->flash_ready = 0;
	} else {
		ctxt->flash_ready = 1;
	}
}


static void strobe_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	strobe_context_t	*ctxt = (strobe_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	if (!ctxt->flash)
		return til_fb_fragment_clear(fragment);

	til_fb_fragment_fill(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, 0xffffffff);
}


static void strobe_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned int ticks, til_fb_fragment_t **fragment_ptr)
{
	strobe_context_t	*ctxt = (strobe_context_t *)context;

	if (!ctxt->flash)
		return;

	ctxt->flash = 0;
	ctxt->ticks = ticks;
}


static int strobe_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*period;
	const char	*period_values[] = {
				".0166",
				".02",
				".025",
				".05",
				".1",
				".25",
				".5",
				"1",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Strobe period",
							.key = "period",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(STROBE_DEFAULT_PERIOD),
							.values = period_values,
							.annotations = NULL
						},
						&period,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		strobe_setup_t	*setup;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		sscanf(period, "%f", &setup->period);

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	strobe_module = {
	.create_context = strobe_create_context,
	.prepare_frame = strobe_prepare_frame,
	.render_fragment = strobe_render_fragment,
	.finish_frame = strobe_finish_frame,
	.setup = strobe_setup,
	.name = "strobe",
	.description = "Strobe light (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
