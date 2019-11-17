#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "settings.h"
#include "setup.h"
#include "fb.h"
#include "fps.h"
#include "rototiller.h"
#include "threads.h"
#include "util.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

#define NUM_FB_PAGES	3
/* ^ By triple-buffering, we can have a page tied up being displayed, another
 * tied up submitted and waiting for vsync, and still not block on getting
 * another page so we can begin rendering another frame before vsync.  With
 * just two pages we end up twiddling thumbs until the vsync arrives.
 */
#define DEFAULT_MODULE	"rtv"
#define DEFAULT_VIDEO	"sdl"

extern fb_ops_t			drm_fb_ops;
extern fb_ops_t			sdl_fb_ops;
fb_ops_t			*fb_ops;

extern rototiller_module_t	flui2d_module;
extern rototiller_module_t	julia_module;
extern rototiller_module_t	pixbounce_module;
extern rototiller_module_t	plasma_module;
extern rototiller_module_t	ray_module;
extern rototiller_module_t	roto_module;
extern rototiller_module_t	rtv_module;
extern rototiller_module_t	snow_module;
extern rototiller_module_t	sparkler_module;
extern rototiller_module_t	stars_module;
extern rototiller_module_t	submit_module;

static const rototiller_module_t	*modules[] = {
	&flui2d_module,
	&julia_module,
	&pixbounce_module,
	&plasma_module,
	&ray_module,
	&roto_module,
	&rtv_module,
	&snow_module,
	&sparkler_module,
	&stars_module,
	&submit_module,
};

typedef struct rototiller_t {
	const rototiller_module_t	*module;
	void				*module_context;
	threads_t			*threads;
	pthread_t			thread;
	fb_t				*fb;
} rototiller_t;

static rototiller_t		rototiller;


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


static void module_render_fragment(const rototiller_module_t *module, void *context, threads_t *threads, fb_fragment_t *fragment)
{
	if (module->prepare_frame) {
		rototiller_fragmenter_t	fragmenter;

		module->prepare_frame(context, threads_num_threads(threads), fragment, &fragmenter);

		if (module->render_fragment) {
			threads_frame_submit(threads, fragment, fragmenter, module->render_fragment, context);
			threads_wait_idle(threads);
		}

	} else if (module->render_fragment)
		module->render_fragment(context, fragment);

	if (module->finish_frame)
		module->finish_frame(context, fragment);
}


/* This is a public interface to the threaded module rendering intended for use by
 * modules that wish to get the output of other modules for their own use.
 */
void rototiller_module_render(const rototiller_module_t *module, void *context, fb_fragment_t *fragment)
{
	module_render_fragment(module, context, rototiller.threads, fragment);
}


typedef struct argv_t {
	const char	*module;
	const char	*video;

	unsigned	use_defaults:1;
	unsigned	help:1;
} argv_t;

/*
 * ./rototiller --video=drm,dev=/dev/dri/card3,connector=VGA-1,mode=640x480@60
 * ./rototiller --video=sdl,size=640x480
 * ./rototiller --module=roto,foo=bar,module=settings
 * ./rototiller --defaults
 */
static int parse_argv(int argc, const char *argv[], argv_t *res_args)
{
	int	i;

	assert(argc > 0);
	assert(argv);
	assert(res_args);

	/* this is intentionally being kept very simple, no new dependencies like getopt. */

	for (i = 1; i < argc; i++) {
		if (!strncmp("--video=", argv[i], 8)) {
			res_args->video = &argv[i][8];
		} else if (!strncmp("--module=", argv[i], 9)) {
			res_args->module = &argv[i][9];
		} else if (!strcmp("--defaults", argv[i])) {
			res_args->use_defaults = 1;
		} else if (!strcmp("--help", argv[i])) {
			res_args->help = 1;
		} else {
			return -EINVAL;
		}
	}

	return 0;
}


typedef struct setup_t {
	settings_t	*module;
	settings_t	*video;
} setup_t;

/* FIXME: this is unnecessarily copy-pasta, i think modules should just be made
 * more generic to encompass the setting up uniformly, then basically
 * subclass the video backend vs. renderer stuff.
 */

/* select video backend if not yet selected, then setup the selected backend. */
static int setup_video(settings_t *settings, setting_desc_t **next_setting)
{
	const char	*video;

	/* XXX: there's only one option currently, so this is simple */
	video = settings_get_key(settings, 0);
	if (!video) {
		setting_desc_t	*desc;
		const char	*values[] = {
#ifdef HAVE_DRM
					"drm",
#endif
					"sdl",
					NULL,
				};
		int		r;

		r = setting_desc_clone(&(setting_desc_t){
						.name = "Video Backend",
						.key = NULL,
						.regex = "[a-z]+",
						.preferred = DEFAULT_VIDEO,
						.values = values,
						.annotations = NULL
					}, next_setting);
		if (r < 0)
			return r;

		return 1;
	}

	/* XXX: this is kind of hacky for now */
#ifdef HAVE_DRM
	if (!strcmp(video, "drm")) {
		fb_ops = &drm_fb_ops;

		return drm_fb_ops.setup(settings, next_setting);
	} else
#endif
	if (!strcmp(video, "sdl")) {
		fb_ops = &sdl_fb_ops;

		return sdl_fb_ops.setup(settings, next_setting);
	}

	return -EINVAL;
}


/* select module if not yet selected, then setup the module. */
static int setup_module(settings_t *settings, setting_desc_t **next_setting)
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


/* turn args into settings, automatically applying defaults if appropriate, or interactively if appropriate. */
/* returns negative value on error, 0 when settings unchanged from args, 1 when changed */
static int setup_from_args(argv_t *args, setup_t *res_setup)
{
	int	r, changes = 0;
	setup_t	setup;

	setup.module = settings_new(args->module);
	if (!setup.module)
		return -ENOMEM;

	setup.video = settings_new(args->video);
	if (!setup.video) {
		settings_free(setup.module);

		return -ENOMEM;
	}

	r = setup_interactively(setup.module, setup_module, args->use_defaults);
	if (r < 0) {
		settings_free(setup.module);
		settings_free(setup.video);

		return r;
	}

	if (r)
		changes = 1;

	r = setup_interactively(setup.video, setup_video, args->use_defaults);
	if (r < 0) {
		settings_free(setup.module);
		settings_free(setup.video);

		return r;
	}

	if (r)
		changes = 1;

	*res_setup = setup;

	return changes;
}


static int print_setup_as_args(setup_t *setup)
{
	char	*module_args, *video_args;
	char	buf[64];
	int	r;

	module_args = settings_as_arg(setup->module);
	if (!module_args) {
		r = -ENOMEM;

		goto _out;
	}

	video_args = settings_as_arg(setup->video);
	if (!video_args) {
		r = -ENOMEM;

		goto _out_module;
	}

	r = printf("\nConfigured settings as flags:\n  --module=%s --video=%s\n\nPress enter to continue...\n",
		module_args,
		video_args);

	if (r < 0)
		goto _out_video;

	(void) fgets(buf, sizeof(buf), stdin);

_out_video:
	free(video_args);
_out_module:
	free(module_args);
_out:
	return r;
}


static int print_help(void)
{
	return printf(
		"Run without any flags or partial settings for interactive mode.\n"
		"\n"
		"Supported flags:\n"
		"  --defaults	use defaults for unspecified settings\n"
		"  --help	this help\n"
		"  --module=	module settings\n"
		"  --video=	video settings\n"
		);
}


static void * rototiller_thread(void *_rt)
{
	rototiller_t	*rt = _rt;

	for (;;) {
		fb_page_t	*page;

		page = fb_page_get(rt->fb);
		module_render_fragment(rt->module, rt->module_context, rt->threads, &page->fragment);
		fb_page_put(rt->fb, page);
	}

	return NULL;
}


/* When run with partial/no arguments, if stdin is a tty, enter an interactive setup.
 * If stdin is not a tty, or if --defaults is supplied in argv, default settings are used.
 * If any changes to the settings occur in the course of execution, either interactively or
 * throught --defaults, then print out the explicit CLI invocation usable for reproducing
 * the invocation.
 */
int main(int argc, const char *argv[])
{
	setup_t		setup = {};
	argv_t		args = {};
	int		r;

	exit_if(parse_argv(argc, argv, &args) < 0,
		"unable to process arguments");

	if (args.help)
		return print_help() < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

	exit_if((r = setup_from_args(&args, &setup)) < 0,
		"unable to setup: %s", strerror(-r));

	exit_if(r && print_setup_as_args(&setup) < 0,
		"unable to print setup");

	exit_if(!(rototiller.module = rototiller_lookup_module(settings_get_key(setup.module, 0))),
		"unable to lookup module from settings \"%s\"", settings_get_key(setup.module, 0));

	exit_if(!(rototiller.fb = fb_new(fb_ops, setup.video, NUM_FB_PAGES)),
		"unable to create fb");

	exit_if(!fps_setup(),
		"unable to setup fps counter");

	exit_if(rototiller.module->create_context &&
		!(rototiller.module_context = rototiller.module->create_context()),
		"unable to create module context");

	pexit_if(!(rototiller.threads = threads_create()),
		"unable to create rendering threads");

	pexit_if(pthread_create(&rototiller.thread, NULL, rototiller_thread, &rototiller) != 0,
		"unable to create dispatch thread");

	for (;;) {
		if (fb_flip(rototiller.fb) < 0)
			break;

		fps_print(rototiller.fb);
	}

	pthread_cancel(rototiller.thread);
	pthread_join(rototiller.thread, NULL);
	threads_destroy(rototiller.threads);

	if (rototiller.module_context)
		rototiller.module->destroy_context(rototiller.module_context);

	fb_free(rototiller.fb);

	return EXIT_SUCCESS;
}
