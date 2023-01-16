#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_util.h"

#include "txt/txt.h"

/* Copyright (C) 2023 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements a rudimentary sequencing module varying
 * "tapped" variables of other modules on a timeline via
 * GNU Rocket.
 */

typedef struct rocket_context_t {
	til_module_context_t	til_module_context;

	const til_module_t	*module;
	til_module_context_t	*module_ctxt;
	char			*module_settings;
} rocket_context_t;

typedef struct rocket_setup_t {
	til_setup_t		til_setup;
	const char		*module;
} rocket_setup_t;

static rocket_setup_t rocket_default_setup = { .module = "rtv" };


static til_module_context_t * rocket_create_context(til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	rocket_context_t	*ctxt;
	const til_module_t	*module;

	if (!setup)
		setup = &rocket_default_setup.til_setup;

	module = til_lookup_module(((rocket_setup_t *)setup)->module);
	if (!module)
		return NULL;

	ctxt = til_module_context_new(stream, sizeof(rocket_context_t), seed, ticks, n_cpus, path);
	if (!ctxt)
		return NULL;

	ctxt->module = module;

	{
		til_setup_t	*module_setup = NULL;

		(void) til_module_randomize_setup(ctxt->module, rand_r(&seed), &module_setup, NULL);

		(void) til_module_create_context(ctxt->module, stream, rand_r(&seed), ticks, 0, path, module_setup, &ctxt->module_ctxt);
		til_setup_free(module_setup);
	}

	return &ctxt->til_module_context;
}


static void rocket_destroy_context(til_module_context_t *context)
{
	rocket_context_t	*ctxt = (rocket_context_t *)context;

	til_module_context_free(ctxt->module_ctxt);
	free(context);
}


static void rocket_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	rocket_context_t	*ctxt = (rocket_context_t *)context;

	til_module_render(ctxt->module_ctxt, stream, ticks, fragment_ptr);
}


static int rocket_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*module;
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Module to sequence",
							.key = "module",
							.preferred = "rtv",
							.annotations = NULL,
						},
						&module,
						res_setting,
						res_desc);
	if (r)
		return r;

	/* turn layers colon-separated list into a null-terminated array of strings */
	if (res_setup) {
		const til_module_t	*til_module;
		rocket_setup_t		*setup;

		if (!strcmp(module, "rocket"))
			return -EINVAL;

		til_module = til_lookup_module(module);
		if (!til_module)
			return -ENOENT;

		if (til_module->flags & (TIL_MODULE_HERMETIC | TIL_MODULE_EXPERIMENTAL))
			return -EINVAL;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		setup->module = til_module->name;

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	rocket_module = {
	.create_context = rocket_create_context,
	.destroy_context = rocket_destroy_context,
	.render_fragment = rocket_render_fragment,
	.name = "rocket",
	.description = "GNU Rocket module sequencer",
	.setup = rocket_setup,
	.flags = TIL_MODULE_HERMETIC | TIL_MODULE_EXPERIMENTAL,
};
