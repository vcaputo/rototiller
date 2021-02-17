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

#include "settings.h"
#include "fb.h"
#include "rototiller.h"
#include "threads.h"
#include "util.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

#define DEFAULT_MODULE	"rtv"

static threads_t		*rototiller_threads;

extern rototiller_module_t	compose_module;
extern rototiller_module_t	drizzle_module;
extern rototiller_module_t	flui2d_module;
extern rototiller_module_t	julia_module;
extern rototiller_module_t	meta2d_module;
extern rototiller_module_t	montage_module;
extern rototiller_module_t	pixbounce_module;
extern rototiller_module_t	plasma_module;
extern rototiller_module_t	plato_module;
extern rototiller_module_t	ray_module;
extern rototiller_module_t	roto_module;
extern rototiller_module_t	rtv_module;
extern rototiller_module_t	snow_module;
extern rototiller_module_t	sparkler_module;
extern rototiller_module_t	spiro_module;
extern rototiller_module_t	stars_module;
extern rototiller_module_t	submit_module;
extern rototiller_module_t	swab_module;
extern rototiller_module_t	swarm_module;

static const rototiller_module_t	*modules[] = {
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
int rototiller_init(void)
{
	if (!(rototiller_threads = threads_create()))
		return -errno;

	return 0;
}


/* wait for all threads to be idle */
void rototiller_quiesce(void)
{
	threads_wait_idle(rototiller_threads);
}


void rototiller_shutdown(void)
{
	threads_destroy(rototiller_threads);
}


const rototiller_module_t * rototiller_lookup_module(const char *name)
{
	assert(name);

	for (size_t i = 0; i < nelems(modules); i++) {
		if (!strcasecmp(name, modules[i]->name))
			return modules[i];
	}

	return NULL;
}


void rototiller_get_modules(const rototiller_module_t ***res_modules, size_t *res_n_modules)
{
	assert(res_modules);
	assert(res_n_modules);

	*res_modules = modules;
	*res_n_modules = nelems(modules);
}


static void module_render_fragment(const rototiller_module_t *module, void *context, threads_t *threads, unsigned ticks, fb_fragment_t *fragment)
{
	assert(module);
	assert(threads);
	assert(fragment);

	if (module->prepare_frame) {
		rototiller_fragmenter_t	fragmenter;

		module->prepare_frame(context, ticks, threads_num_threads(threads), fragment, &fragmenter);

		if (module->render_fragment) {
			threads_frame_submit(threads, fragment, fragmenter, module->render_fragment, context, ticks);
			threads_wait_idle(threads);
		}

	} else if (module->render_fragment)
		module->render_fragment(context, ticks, 0, fragment);

	if (module->finish_frame)
		module->finish_frame(context, ticks, fragment);
}


/* This is a public interface to the threaded module rendering intended for use by
 * modules that wish to get the output of other modules for their own use.
 */
void rototiller_module_render(const rototiller_module_t *module, void *context, unsigned ticks, fb_fragment_t *fragment)
{
	module_render_fragment(module, context, rototiller_threads, ticks, fragment);
}

int rototiller_module_create_context(const rototiller_module_t *module, unsigned ticks, void **res_context)
{
	void	*context;

	assert(module);
	assert(res_context);

	if (!module->create_context)
		return 0;

	context = module->create_context(ticks, threads_num_threads(rototiller_threads));
	if (!context)
		return -ENOMEM;

	*res_context = context;

	return 0;
}


/* select module if not yet selected, then setup the module. */
int rototiller_module_setup(settings_t *settings, setting_desc_t **next_setting)
{
	const rototiller_module_t	*module;
	const char			*name;

	name = settings_get_key(settings, 0);
	if (!name) {
		const char	*values[nelems(modules) + 1] = {};
		const char	*annotations[nelems(modules) + 1] = {};
		setting_desc_t	*desc;
		unsigned	i;
		int		r;

		for (i = 0; i < nelems(modules); i++) {
			values[i] = modules[i]->name;
			annotations[i] = modules[i]->description;
		}

		r = setting_desc_clone(&(setting_desc_t){
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

	module = rototiller_lookup_module(name);
	if (!module)
		return -EINVAL;

	if (module->setup)
		return module->setup(settings, next_setting);

	return 0;
}
