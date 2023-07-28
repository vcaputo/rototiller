#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
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
#include "til_stream.h"
#include "til_threads.h"
#include "til_util.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

#define TIL_DEFAULT_ROOT_MODULE		"rtv"
#define TIL_DEFAULT_NESTED_MODULE	"compose"

static til_threads_t	*til_threads;
static struct timeval	til_start_tv;

extern til_module_t	blinds_module;
extern til_module_t	checkers_module;
extern til_module_t	compose_module;
extern til_module_t	drizzle_module;
extern til_module_t	flui2d_module;
extern til_module_t	julia_module;
extern til_module_t	meta2d_module;
extern til_module_t	mixer_module;
extern til_module_t	moire_module;
extern til_module_t	montage_module;
extern til_module_t	pixbounce_module;
extern til_module_t	plasma_module;
extern til_module_t	plato_module;
extern til_module_t	ray_module;
extern til_module_t	rkt_module;
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
	&mixer_module,
	&moire_module,
	&montage_module,
	&pixbounce_module,
	&plasma_module,
	&plato_module,
	&ray_module,
	&rkt_module,
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

	gettimeofday(&til_start_tv, NULL);

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


#ifdef __WIN32__
/* taken from glibc <sys/time.h> just to make windows happy*/
# define timersub(a, b, result)                                               \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                          \
    if ((result)->tv_usec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_usec += 1000000;                                           \
    }                                                                         \
  } while (0)
#endif

/* returns number of "ticks" since til_init(), which are currently milliseconds */
unsigned til_ticks_now(void)
{
	struct timeval	now, diff;

	/* for profiling purposes in particular, it'd be nice to bump up to microseconds...
	 * but then it'll prolly need uint64_t
	 */

	gettimeofday(&now, NULL);
	timersub(&now, &til_start_tv, &diff);

	return diff.tv_sec * 1000 + diff.tv_usec / 1000;
}


/* "blank" built-in module */
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


/* "noop" built-in module */
static til_module_t	_noop_module = {
	.name = "noop",
	.description = "built-in nothing-doer",
	.author = "built-in",
};


/* "ref" built-in module */
#include "libs/txt/txt.h" /* for rendering some diagnostics */

typedef struct _ref_setup_t {
	til_setup_t		til_setup;

	char			*path;
} _ref_setup_t;


typedef struct _ref_context_t {
	til_module_context_t	til_module_context;

	til_module_context_t	*ref;
} _ref_context_t;


static til_module_context_t * _ref_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	_ref_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(*ctxt), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	return &ctxt->til_module_context;
}


static void _ref_destroy_context(til_module_context_t *context)
{
	_ref_context_t	*ctxt = (_ref_context_t *)context;

	ctxt->ref = til_module_context_free(ctxt->ref);
	free(context);
}


static void _ref_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	_ref_context_t	*ctxt = (_ref_context_t *)context;
	_ref_setup_t	*s = (_ref_setup_t *)context->setup;

	if (!ctxt->ref) {
		int	r;

		/* TODO: switch to til_stream_find_module_context(), this clones concept is DOA. */
		r = til_stream_find_module_contexts(stream, s->path, 1, &ctxt->ref);
		if (r < 0) {
			txt_t	*msg = txt_newf("%s: BAD PATH \"%s\"", context->setup->path, s->path);

			til_fb_fragment_clear(*fragment_ptr);
			txt_render_fragment(msg, *fragment_ptr, 0xffffffff,
					    0, 0,
					    (txt_align_t){
						.horiz = TXT_HALIGN_LEFT,
						.vert = TXT_VALIGN_TOP,
					    });
			txt_free(msg);
			/* TODO: maybe print all available contexts into the fragment? */
			return;
		}
	}

	til_module_render_limited(ctxt->ref, stream, ticks, context->n_cpus, fragment_ptr);
}


static void _ref_setup_free(til_setup_t *setup)
{
	_ref_setup_t	*s = (_ref_setup_t *)setup;

	free(s->path);
	free(s);
}


static int _ref_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*path;
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Context path to reference",
							.key = "path",
							.regex = "[a-zA-Z0-9/_]+",
							.preferred = "",
						},
						&path,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		_ref_setup_t    *setup;

		setup = til_setup_new(settings, sizeof(*setup), _ref_setup_free);
		if (!setup)
			return -ENOMEM;

		setup->path = strdup(path);
		if (!setup->path) {
			til_setup_free(&setup->til_setup);

			return -ENOMEM;
	       }

		*res_setup = &setup->til_setup;
	}

	return 0;
}


static til_module_t	_ref_module = {
	.create_context = _ref_create_context,
	.destroy_context = _ref_destroy_context,
	.render_fragment = _ref_render_fragment,
	.setup = _ref_setup,
	.name = "ref",
	.description = "built-in context referencer",
	.author = "built-in",
};


const til_module_t * til_lookup_module(const char *name)
{
	static const til_module_t       *builtins[] = {
					       &_blank_module,
					       &_noop_module,
					       &_ref_module,
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


char * til_get_module_names(unsigned flags_excluded, const char **exclusions)
{
	const til_module_t	**modules;
	size_t			n_modules;
	size_t			bufsz;
	char			*buf;

	til_get_modules(&modules, &n_modules);

	for (buf = NULL, bufsz = sizeof('\0');;) {
		for (size_t i = 0, j = 0, p = 0; i < n_modules; i++) {
			const til_module_t	*mod = modules[i];
			const char		**exclusion = exclusions;

			if ((mod->flags & flags_excluded))
				continue;

			while (*exclusion) {
				if (!strcmp(*exclusion, mod->name))
					break;

				exclusion++;
			}

			if (*exclusion)
				continue;

			if (!buf)
				bufsz += snprintf(NULL, 0, "%s%s", j ? "," : "", mod->name);
			else
				p += snprintf(&buf[p], bufsz - p, "%s%s", j ? "," : "", mod->name);

			j++;
		}

		if (buf)
			return buf;

		buf = calloc(1, bufsz);
		if (!buf)
			return NULL;
	}
}


static void module_render_fragment(til_module_context_t *context, til_stream_t *stream, til_threads_t *threads, unsigned n_cpus, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	const til_module_t	*module;
	int			touched = 0;

	assert(context);
	assert(context->module);
	assert(threads);
	assert(fragment_ptr && *fragment_ptr);
	assert(n_cpus);

	module = context->module;

	if (module->prepare_frame) {
		til_frame_plan_t	frame_plan = {};

		module->prepare_frame(context, stream, ticks, fragment_ptr, &frame_plan);

		/* XXX: any module which provides prepare_frame() must return a frame_plan.fragmenter,
		 * and provide render_fragment()
		 */
		assert(frame_plan.fragmenter);
		assert(module->render_fragment);

		if (n_cpus > 1) {
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
		touched++;
	} else if (module->render_fragment) {
		module->render_fragment(context, stream, ticks, 0, fragment_ptr);
		touched++;
	}

	if (module->finish_frame) {
		module->finish_frame(context, stream, ticks, fragment_ptr);
		touched++;
	}

	(*fragment_ptr)->cleared = !!touched;
}


static void _til_module_render(til_module_context_t *context, til_stream_t *stream, unsigned n_cpus, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	unsigned	start = til_ticks_now();

	module_render_fragment(context, stream, til_threads, n_cpus, ticks, fragment_ptr);

	context->last_render_duration = til_ticks_now() - start;
	if (context->last_render_duration > context->max_render_duration)
		context->max_render_duration = context->last_render_duration;
	context->renders_count++;
	context->last_ticks = ticks;
}


/* This is a public interface to the threaded module rendering intended for use by
 * modules that wish to get the output of other modules for their own use.
 */
void til_module_render(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	return _til_module_render(context, stream, context->n_cpus, ticks, fragment_ptr);
}


/* Identical to til_module_render() except with a parameterized upper bound for context->c_cpus,
 * this is primarily intended for modules performing nested rendering
 */
void til_module_render_limited(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned max_cpus, til_fb_fragment_t **fragment_ptr)
{
	return _til_module_render(context, stream, MIN(context->n_cpus, max_cpus), ticks, fragment_ptr);
}


/* if n_cpus == 0, it will be automatically set to n_threads.
 * to explicitly set n_cpus, just pass the value.  This is primarily intended for
 * the purpose of explicitly constraining rendering parallelization to less than n_threads,
 * if n_cpus is specified > n_threads it won't increase n_threads...
 *
 * if stream is non-NULL, the created contexts will be registered on-stream w/handle @setup->path.
 * any existing contexts @setup->path, will be replaced by the new one.
 */
int til_module_create_contexts(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup, size_t n_contexts, til_module_context_t **res_contexts)
{

	assert(module);
	assert(setup); /* we *always* want a setup, even if the module has no setup() method - for the path */
	assert(n_contexts > 0);
	assert(res_contexts);

	if (!n_cpus)
		n_cpus = til_threads_num_threads(til_threads);

	for (size_t i = 0; i < n_contexts; i++) {
		til_module_context_t	*context;

		if (!module->create_context)
			context  = til_module_context_new(module, sizeof(til_module_context_t), stream, seed, ticks, n_cpus, setup);
		else
			context = module->create_context(module, stream, seed, ticks, n_cpus, setup);

		if (!context) {
			for (size_t j = 0; j < i; j++)
				res_contexts[j] = til_module_context_free(res_contexts[j]);

			return -ENOMEM;
		}

		res_contexts[i] = context;
	}

	if (stream) {
		int	r;

		r = til_stream_register_module_contexts(stream, n_contexts, res_contexts);
		if (r < 0) {
			for (size_t i = 0; i < n_contexts; i++)
				res_contexts[i] = til_module_context_free(res_contexts[i]);

			return r;
		}
	}

	return 0;
}


/* convenience single-context wrapper around til_module_create_contexts(), as most callers need just one. */
int til_module_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup, til_module_context_t **res_context)
{
	return til_module_create_contexts(module, stream, seed, ticks, n_cpus, setup, 1, res_context);
}


int til_module_setup_full(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup, const char *name, const char *preferred, unsigned flags_excluded, const char **exclusions)
{
	const char		*module_name;
	til_setting_t		*setting;
	const til_module_t	*module;

	assert(settings);
	assert(name);
	assert(preferred);

	module_name = til_settings_get_value_by_idx(settings, 0, &setting);
	if (!module_name || !setting->desc) {
		const char		*values[nelems(modules) + 1] = {};
		const char		*annotations[nelems(modules) + 1] = {};
		int			r, filtered = 1;

		/* Only disable filtering if a *valid* module_name is providead.
		 * This is so fat-fingered names don't suddenly include a bunch of broken options.
		 */
		if (module_name && til_lookup_module(module_name))
			filtered = 0;

		for (unsigned i = 0, j = 0; i < nelems(modules); i++) {
			if (filtered) {
				if (modules[i]->flags & flags_excluded)
					continue;

				if (exclusions) {
					const char	*excl = *exclusions;

					for (; excl; excl++) {
						if (!strcasecmp(excl, modules[i]->name))
							break;
					}

					if (excl)
						continue;
				}
			}

			values[j] = modules[i]->name;
			annotations[j] = modules[i]->description;
			j++;
		}

		r = til_setting_desc_new(	settings,
						&(til_setting_spec_t){
							.name = name,
							.key = NULL,
							.regex = "[a-zA-Z0-9]+",
							.preferred = preferred,
							.values = values,
							.annotations = annotations,
							.as_label = 1
						}, res_desc);
		if (r < 0)
			return r;

		*res_setting = module_name ? setting : NULL;

		return 1;
	}

	module = til_lookup_module(module_name);
	if (!module)
		return -EINVAL;

	if (module->setup)
		return module->setup(settings, res_setting, res_desc, res_setup);

	if (res_setup)
		return til_module_setup_finalize(module, settings, res_setup);

	return 0;
}


/* select module if not yet selected, then setup the module. */
int til_module_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const til_settings_t	*parent;

	parent = til_settings_get_parent(settings);

	return til_module_setup_full(settings,
				     res_setting,
				     res_desc,
				     res_setup,
				     "Renderer Module",
				     parent ? TIL_DEFAULT_NESTED_MODULE : TIL_DEFAULT_ROOT_MODULE,
				     (TIL_MODULE_EXPERIMENTAL | (parent ? TIL_MODULE_HERMETIC : 0)),
				     NULL);
}


/* originally taken from rtv, this randomizes a module's setup @res_setup, args @res_arg
 * returns 0 on on setup successful with results stored @res_*, -errno on error.
 */
int til_module_setup_randomize(const til_module_t *module, til_settings_t *settings, unsigned seed, til_setup_t **res_setup, char **res_arg)
{
	til_setting_t			*setting;
	const char			*name;
	const til_setting_desc_t	*desc;
	int				r = 0;

	assert(module);
	assert(settings);

	/* This is kind of a silly formality for randomize, since the callers already
	 * specify the module.  But we really need to ensure the first entry is described,
	 * so the .as_label can be found in situations like rkt_scener's "add randomized scene".
	 *
	 * FIXME TODO: what should probably be happening using til_module_setup() as the
	 * top-level setup_func, to get the module setting described.  This is just a quick
	 * hack to make things usable.
	 */
	name = til_settings_get_value_by_idx(settings, 0, &setting);
	if (!name)
		return -EINVAL; /* TODO: add a first setting from module->name? current callers always pass the module name as the setting string */

	if (!setting->desc) {
		r = til_setting_desc_new(	settings,
						&(til_setting_spec_t){
							.name = "Renderer module",
							.preferred = module->name,
							.as_label = 1
						}, &setting->desc);
		if (r < 0)
			return r;
	}

	if (!module->setup) {
		til_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL);
		if (!setup)
			r = -ENOMEM;
		else
			*res_setup = setup;
	} else {
		for (setting = NULL; module->setup(settings, &setting, &desc, res_setup) > 0; setting = NULL) {
			assert(desc);

			if (!setting) {
				if (desc->spec.random) {
					char	*value;

					value = desc->spec.random(rand_r(&seed));
					setting = til_settings_add_value(desc->container, desc->spec.key, value);
					free(value);
				} else if (desc->spec.values) {
					int	n;

					for (n = 0; desc->spec.values[n]; n++);

					n = rand_r(&seed) % n;

					setting = til_settings_add_value(desc->container, desc->spec.key, desc->spec.values[n]);
				} else {
					setting = til_settings_add_value(desc->container, desc->spec.key, desc->spec.preferred);
				}
			}

			assert(setting);

			if (setting->desc)
				continue;

			/*
			 * TODO This probably also needs to move into a til_settings helper,
			 * copy-n-pasta alert, taken from setup.c
			 */
			if (desc->spec.override) {
				const char	*o;

				o = desc->spec.override(setting->value);
				if (!o)
					return -ENOMEM;

				if (o != setting->value) {
					free((void *)setting->value);
					setting->value = o;
				}
			}

			if (desc->spec.as_nested_settings && !setting->value_as_nested_settings) {
				char	*label = NULL;

				if (!desc->spec.key) {
					/* generate a positional label for bare-value specs */
					r = til_settings_label_setting(desc->container, setting, &label);
					if (r < 0)
						break;
				}

				setting->value_as_nested_settings = til_settings_new(NULL, desc->container, desc->spec.key ? : label, setting->value);
				free(label);

				if (!setting->value_as_nested_settings) {
					r = -ENOMEM;
					break;
				}
			}

			setting->desc = desc;
		}
	}

	if (res_arg && r == 0) {
		char	*arg;

		arg = til_settings_as_arg(settings);
		if (!arg)
			r = -ENOMEM;
		else
			*res_arg = arg;
	}

	return r;
}


/* This turns the incoming module+setings into a "baked" til_setup_t,
 * if module->setup() isn't provided, a minimal til_setup_t is still produced.
 */
int til_module_setup_finalize(const til_module_t *module, const til_settings_t *module_settings, til_setup_t **res_setup)
{
	til_setting_t			*setting;
	const til_setting_desc_t	*desc;
	int				r;

	assert(module);
	assert(module_settings);
	assert(res_setup);

	if (!module->setup) {
		til_setup_t	*setup;

		setup = til_setup_new(module_settings, sizeof(*setup), NULL);
		if (!setup)
			return -ENOMEM;

		*res_setup = setup;

		return 0;
	}

	/* TODO: note passing &setting and &desc when finalizing is really only necessary
	 * because of how nested settings get found via &setting, and modules that do this
	 * currently tend to access (*res_setting)->value_as_nested_settings and that needs
	 * to occur even when just finalizing.  A future change may rework how modules do
	 * this, but let's just pass the res_setting and res_desc pointers to keep things
	 * happy for now.  Long-term it should really be possible to pass NULL for those,
	 * at least when you're just finalizing.
	 */
	r = module->setup(module_settings, &setting, &desc, res_setup);
	if (r < 0)
		return r;
	if (r > 0)	/* FIXME: this should probably free desc */
		return -EINVAL; /* module_settings is incomplete, but we're not performing setup here. */

	return r;
}


/* generic fragmenter using a horizontal slice per cpu according to context->n_cpus (multiplied by a constant factor) */
int til_fragmenter_slice_per_cpu(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	/* The *16 is to combat leaving CPUs idle waiting for others to finish their work.
	 *
	 * Even though there's some overhead in scheduling smaller work units,
	 * this still tends to result in better aggregate CPU utilization, up
	 * to a point.  The cost of rendering slices is often inconsistent,
	 * and there's always a delay from one thread to another getting
	 * started on their work, as well as scheduling variance.
	 *
	 * So it's beneficial to enable early finishers to pick
	 * up slack of the laggards via slightly more granular
	 * work units.
	 */
	return til_fb_fragment_slice_single(fragment, context->n_cpus * 16, number, res_fragment);
}


/* generic fragmenter using 64x64 tiles */
int til_fragmenter_tile64(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	return til_fb_fragment_tile_single(fragment, 64, number, res_fragment);
}
