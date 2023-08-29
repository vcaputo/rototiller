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
 *	- Make hz setting more flexible
 */

#define	STROBE_DEFAULT_HZ		10

typedef struct strobe_setup_t {
	til_setup_t		til_setup;
	float			hz;
} strobe_setup_t;

typedef struct strobe_context_t {
	til_module_context_t	til_module_context;
	strobe_setup_t		*setup;
	unsigned		ticks;
	unsigned		flash:1;
	unsigned		flash_ready:1;
} strobe_context_t;


static til_module_context_t * strobe_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	strobe_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(strobe_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->setup = (strobe_setup_t *)setup;
	ctxt->ticks = ticks;

	return &ctxt->til_module_context;
}


static void strobe_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	strobe_context_t	*ctxt = (strobe_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu_x16 };

	if (ctxt->flash_ready && (ticks - ctxt->ticks >= (unsigned)((1.f / ctxt->setup->hz) * 1000.f))){
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


static int strobe_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


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


static int strobe_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*hz;
	const char	*hz_values[] = {
				"60",
				"50",
				"40",
				"20",
				"10",
				"4",
				"2",
				"1",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Strobe frequency in hz",
							.key = "hz",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(STROBE_DEFAULT_HZ),
							.values = hz_values,
							.annotations = NULL
						},
						&hz,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		strobe_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &strobe_module);
		if (!setup)
			return -ENOMEM;

		sscanf(hz, "%f", &setup->hz);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
