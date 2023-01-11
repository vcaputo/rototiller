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
#include "til_module_context.h"
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
extern til_module_t	moire_module;
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
extern til_module_t	strobe_module;
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
	&moire_module,
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
	&strobe_module,
	&submit_module,
	&swab_module,
	&swarm_module,
	&voronoi_module,
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


static void _blank_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu };
}


static void _blank_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	til_fb_fragment_clear(*fragment_ptr);
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


static void module_render_fragment(til_module_context_t *context, til_stream_t *stream, til_threads_t *threads, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	const til_module_t	*module;

	assert(context);
	assert(context->module);
	assert(threads);
	assert(fragment_ptr && *fragment_ptr);

	module = context->module;

	if (module->prepare_frame) {
		til_frame_plan_t	frame_plan = {};

		module->prepare_frame(context, stream, ticks, fragment_ptr, &frame_plan);

		/* XXX: any module which provides prepare_frame() must return a frame_plan.fragmenter,
		 * and provide render_fragment()
		 */
		assert(frame_plan.fragmenter);
		assert(module->render_fragment);

		if (context->n_cpus > 1) {
			til_threads_frame_submit(threads, fragment_ptr, &frame_plan, module->render_fragment, context, stream, ticks);
			til_threads_wait_idle(threads);
		} else {
			unsigned		fragnum = 0;
			til_fb_fragment_t	frag, texture, *frag_ptr = &frag;

			if ((*fragment_ptr)->texture)
				frag.texture = &texture; /* fragmenter needs the space */

			while (frame_plan.fragmenter(context, *fragment_ptr, fragnum++, &frag))
				module->render_fragment(context, stream, ticks, 0, &frag_ptr);
		}
	} else if (module->render_fragment)
		module->render_fragment(context, stream, ticks, 0, fragment_ptr);

	if (module->finish_frame)
		module->finish_frame(context, stream, ticks, fragment_ptr);

	(*fragment_ptr)->cleared = 1;
}


/* This is a public interface to the threaded module rendering intended for use by
 * modules that wish to get the output of other modules for their own use.
 */
void til_module_render(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	module_render_fragment(context, stream, til_threads, ticks, fragment_ptr);
}


/* if n_cpus == 0, it will be automatically set to n_threads.
 * to explicitly set n_cpus, just pass the value.  This is primarily intended for
 * the purpose of explicitly constraining rendering parallelization to less than n_threads,
 * if n_cpus is specified > n_threads it won't increase n_threads...
 */
int til_module_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, const char *parent_path, til_setup_t *setup, til_module_context_t **res_context)
{
	til_module_context_t	*context;
	char			*path;

	assert(module);
	assert(parent_path);
	assert(res_context);

	{
		size_t	path_len;

		/* TODO: when til_setup_t learns to name settings blocks, this would be where to override module->name with the setup-specified name */
		path_len = strlen(parent_path) + 1 + strlen(module->name) + 1;
		path = calloc(1, path_len);
		if (!path)
			return -ENOMEM;

		snprintf(path, path_len, "%s/%s", parent_path, module->name);
	}

	if (!n_cpus)
		n_cpus = til_threads_num_threads(til_threads);

	if (!module->create_context)
		context = til_module_context_new(stream, sizeof(til_module_context_t), seed, ticks, n_cpus, path);
	else
		context = module->create_context(stream, seed, ticks, n_cpus, path, setup);

	if (!context) {
		free(path);

		return -ENOMEM;
	}

	context->module = module;

	*res_context = context;

	return 0;
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
int til_module_randomize_setup(const til_module_t *module, unsigned seed, til_setup_t **res_setup, char **res_arg)
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

			value = desc->random(rand_r(&seed));
			til_settings_add_value(settings, desc->key, value, desc);
			free(value);
		} else if (desc->values) {
			int	n;

			for (n = 0; desc->values[n]; n++);

			n = rand_r(&seed) % n;

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


/* generic fragmenter using a horizontal slice per cpu according to context->n_cpus */
int til_fragmenter_slice_per_cpu(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	return til_fb_fragment_slice_single(fragment, context->n_cpus, number, res_fragment);
}


/* generic fragmenter using 64x64 tiles */
int til_fragmenter_tile64(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	return til_fb_fragment_tile_single(fragment, 64, number, res_fragment);
}
