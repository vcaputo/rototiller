#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_settings.h"
#include "til_threads.h"
#include "til_util.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

#define DEFAULT_MODULE	"rtv"

static til_threads_t	*til_threads;

extern til_module_t	compose_module;
extern til_module_t	drizzle_module;
extern til_module_t	flui2d_module;
extern til_module_t	julia_module;
extern til_module_t	meta2d_module;
extern til_module_t	montage_module;
extern til_module_t	pixbounce_module;
extern til_module_t	plasma_module;
extern til_module_t	plato_module;
extern til_module_t	ray_module;
extern til_module_t	roto_module;
extern til_module_t	rtv_module;
extern til_module_t	snow_module;
extern til_module_t	sparkler_module;
extern til_module_t	spiro_module;
extern til_module_t	stars_module;
extern til_module_t	submit_module;
extern til_module_t	swab_module;
extern til_module_t	swarm_module;

static const til_module_t	*modules[] = {
	&compose_module,
	&drizzle_module,
	&flui2d_module,
	&julia_module,
	&meta2d_module,
	&montage_module,
	&pixbounce_module,
	&plasma_module,
	&plato_module,
	&ray_module,
	&roto_module,
	&rtv_module,
	&snow_module,
	&sparkler_module,
	&spiro_module,
	&stars_module,
	&submit_module,
	&swab_module,
	&swarm_module,
};


/* initialize rototiller (create rendering threads) */
int til_init(void)
{
	if (!(til_threads = til_threads_create()))
		return -errno;

	return 0;
}


/* wait for all threads to be idle */
void til_quiesce(void)
{
	til_threads_wait_idle(til_threads);
}


void til_shutdown(void)
{
	til_threads_destroy(til_threads);
}


const til_module_t * til_lookup_module(const char *name)
{
	assert(name);

	for (size_t i = 0; i < nelems(modules); i++) {
		if (!strcasecmp(name, modules[i]->name))
			return modules[i];
	}

	return NULL;
}


void til_get_modules(const til_module_t ***res_modules, size_t *res_n_modules)
{
	assert(res_modules);
	assert(res_n_modules);

	*res_modules = modules;
	*res_n_modules = nelems(modules);
}


static void module_render_fragment(const til_module_t *module, void *context, til_threads_t *threads, unsigned ticks, til_fb_fragment_t *fragment)
{
	assert(module);
	assert(threads);
	assert(fragment);

	if (module->prepare_frame) {
		til_fragmenter_t	fragmenter;

		module->prepare_frame(context, ticks, til_threads_num_threads(threads), fragment, &fragmenter);

		if (module->render_fragment) {
			til_threads_frame_submit(threads, fragment, fragmenter, module->render_fragment, context, ticks);
			til_threads_wait_idle(threads);
		}

	} else if (module->render_fragment)
		module->render_fragment(context, ticks, 0, fragment);

	if (module->finish_frame)
		module->finish_frame(context, ticks, fragment);
}


/* This is a public interface to the threaded module rendering intended for use by
 * modules that wish to get the output of other modules for their own use.
 */
void til_module_render(const til_module_t *module, void *context, unsigned ticks, til_fb_fragment_t *fragment)
{
	module_render_fragment(module, context, til_threads, ticks, fragment);
}


int til_module_create_context(const til_module_t *module, unsigned ticks, void **res_context)
{
	void	*context;

	assert(module);
	assert(res_context);

	if (!module->create_context)
		return 0;

	context = module->create_context(ticks, til_threads_num_threads(til_threads));
	if (!context)
		return -ENOMEM;

	*res_context = context;

	return 0;
}


/* select module if not yet selected, then setup the module. */
int til_module_setup(til_settings_t *settings, til_setting_desc_t **next_setting)
{
	const til_module_t	*module;
	const char		*name;

	name = til_settings_get_key(settings, 0);
	if (!name) {
		const char		*values[nelems(modules) + 1] = {};
		const char		*annotations[nelems(modules) + 1] = {};
		til_setting_desc_t	*desc;
		int			r;

		for (unsigned i = 0; i < nelems(modules); i++) {
			values[i] = modules[i]->name;
			annotations[i] = modules[i]->description;
		}

		r = til_setting_desc_clone(&(til_setting_desc_t){
						.name = "Renderer Module",
						.key = NULL,
						.regex = "[a-zA-Z0-9]+",
						.preferred = DEFAULT_MODULE,
						.values = values,
						.annotations = annotations
					}, next_setting);
		if (r < 0)
			return r;

		return 1;
	}

	module = til_lookup_module(name);
	if (!module)
		return -EINVAL;

	if (module->setup)
		return module->setup(settings, next_setting);

	return 0;
}