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
#include <time.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_settings.h"
#include "til_threads.h"
#include "til_util.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

#define DEFAULT_MODULE	"rtv"

static til_threads_t	*til_threads;

extern til_module_t	blinds_module;
extern til_module_t	checkers_module;
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
extern til_module_t	shapes_module;
extern til_module_t	snow_module;
extern til_module_t	sparkler_module;
extern til_module_t	spiro_module;
extern til_module_t	stars_module;
extern til_module_t	submit_module;
extern til_module_t	swab_module;
extern til_module_t	swarm_module;
extern til_module_t	voronoi_module;

static const til_module_t	*modules[] = {
	&blinds_module,
	&checkers_module,
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
	&shapes_module,
	&snow_module,
	&sparkler_module,
	&spiro_module,
	&stars_module,
	&submit_module,
	&swab_module,
	&swarm_module,
	&voronoi_module,
};


/* initialize rototiller (create rendering threads) */
int til_init(void)
{
	/* Various modules seed srand(), just do it here so they don't need to.
	 * At some point in the future this might become undesirable, if reproducible
	 * pseudo-randomized output is actually desirable.  But that should probably be
	 * achieved using rand_r() anyways, since modules can't prevent others from playing
	 * with srand().
	 */
	srand(time(NULL) + getpid());

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


static void _blank_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter)
{
	*res_fragmenter = til_fragmenter_slice_per_cpu;
}


static void _blank_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	til_fb_fragment_clear(fragment);
}


static til_module_t	_blank_module = {
	.prepare_frame = _blank_prepare_frame,
	.render_fragment = _blank_render_fragment,
	.name = "blank",
	.description = "built-in blanker",
	.author = "built-in",
};


const til_module_t * til_lookup_module(const char *name)
{
	static const til_module_t	*builtins[] = {
						&_blank_module,
					};
	static struct {
		const til_module_t	**modules;
		size_t			n_modules;
	}				module_lists[] = {
						{
							builtins,
							nelems(builtins),
						},
						{	modules,
							nelems(modules),
						}
					};

	assert(name);

	for (size_t n = 0; n < nelems(module_lists); n++) {
		for (size_t i = 0; i < module_lists[n].n_modules; i++) {
			if (!strcasecmp(name, module_lists[n].modules[i]->name))
				return module_lists[n].modules[i];
		}
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


int til_module_create_context(const til_module_t *module, unsigned seed, unsigned ticks, til_setup_t *setup, void **res_context)
{
	void	*context;

	assert(module);
	assert(res_context);

	if (!module->create_context)
		return 0;

	context = module->create_context(seed, ticks, til_threads_num_threads(til_threads), setup);
	if (!context)
		return -ENOMEM;

	*res_context = context;

	return 0;
}


void * til_module_destroy_context(const til_module_t *module, void *context)
{
	assert(module);

	if (!module->destroy_context)
		return NULL;

	module->destroy_context(context);

	return NULL;
}


/* select module if not yet selected, then setup the module. */
int til_module_setup(til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t		*setting;
	const til_module_t	*module;
	const char		*name;

	name = til_settings_get_key(settings, 0, &setting);
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
						.name = "Renderer module",
						.key = NULL,
						.regex = "[a-zA-Z0-9]+",
						.preferred = DEFAULT_MODULE,
						.values = values,
						.annotations = annotations
					}, res_desc);
		if (r < 0)
			return r;

		*res_setting = name ? setting : NULL;

		return 1;
	}

	module = til_lookup_module(name);
	if (!module)
		return -EINVAL;

	if (module->setup)
		return module->setup(settings, res_setting, res_desc, res_setup);

	return 0;
}


/* originally taken from rtv, this randomizes a module's setup @res_setup, args @res_arg
 * returns 0 on no setup, 1 on setup successful with results stored @res_*, -errno on error.
 */
int til_module_randomize_setup(const til_module_t *module, til_setup_t **res_setup, char **res_arg)
{
	til_settings_t			*settings;
	til_setting_t			*setting;
	const til_setting_desc_t	*desc;
	int				r = 1;

	assert(module);

	if (!module->setup)
		return 0;

	settings = til_settings_new(NULL);
	if (!settings)
		return -ENOMEM;

	while (module->setup(settings, &setting, &desc, res_setup) > 0) {
		if (desc->random) {
			char	*value;

			value = desc->random();
			til_settings_add_value(settings, desc->key, value, desc);
			free(value);
		} else if (desc->values) {
			int	n;

			for (n = 0; desc->values[n]; n++);

			n = rand() % n;

			til_settings_add_value(settings, desc->key, desc->values[n], desc);
		} else {
			til_settings_add_value(settings, desc->key, desc->preferred, desc);
		}
	}

	if (res_arg) {
		char	*arg;

		arg = til_settings_as_arg(settings);
		if (!arg)
			r = -ENOMEM;
		else
			*res_arg = arg;
	}

	til_settings_free(settings);

	return r;
}


/* generic fragmenter using a horizontal slice per cpu according to n_cpus */
int til_fragmenter_slice_per_cpu(void *context, unsigned n_cpus, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	return til_fb_fragment_slice_single(fragment, n_cpus, number, res_fragment);
}


/* generic fragmenter using 64x64 tiles, ignores n_cpus */
int til_fragmenter_tile64(void *context, unsigned n_cpus, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	return til_fb_fragment_tile_single(fragment, 64, number, res_fragment);
}
