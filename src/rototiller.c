#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "drm_fb.h"
#include "sdl_fb.h"
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
#define DEFAULT_MODULE	"roto32"
#define DEFAULT_VIDEO	"sdl"

extern fb_ops_t			drm_fb_ops;
extern fb_ops_t			sdl_fb_ops;
fb_ops_t			*fb_ops;

extern rototiller_module_t	julia_module;
extern rototiller_module_t	plasma_module;
extern rototiller_module_t	roto32_module;
extern rototiller_module_t	roto64_module;
extern rototiller_module_t	ray_module;
extern rototiller_module_t	sparkler_module;
extern rototiller_module_t	stars_module;

static rototiller_module_t	*modules[] = {
	&roto32_module,
	&roto64_module,
	&ray_module,
	&sparkler_module,
	&stars_module,
	&plasma_module,
	&julia_module,
};


rototiller_module_t * module_lookup(const char *name)
{
	unsigned	i;

	assert(name);

	for (i = 0; i < nelems(modules); i++) {
		if (!strcasecmp(name, modules[i]->name))
			return modules[i];
	}

	return NULL;
}


static void module_render_page_threaded(rototiller_module_t *module, void *context, threads_t *threads, fb_page_t *page)
{
	rototiller_fragmenter_t	fragmenter;

	module->prepare_frame(context, threads_num_threads(threads), &page->fragment, &fragmenter);

	threads_frame_submit(threads, &page->fragment, fragmenter, module->render_fragment, context);
	threads_wait_idle(threads);
}


static void module_render_page(rototiller_module_t *module, void *context, threads_t *threads, fb_page_t *page)
{
	if (module->prepare_frame)
		module_render_page_threaded(module, context, threads, page);
	else
		module->render_fragment(context, &page->fragment);

	if (module->finish_frame)
		module->finish_frame(context, &page->fragment);
}


typedef struct argv_t {
	const char	*module;
	const char	*video;

	unsigned	defaults:1;
	unsigned	help:1;
} argv_t;

/*
 * ./rototiller --video=drm,dev=/dev/dri/card3,connector=VGA-1,mode=640x480@60
 * ./rototiller --video=sdl,size=640x480
 * ./rototiller --module=roto,foo=bar,module=settings
 * ./rototiller --defaults
 */
int parse_argv(int argc, const char *argv[], argv_t *res_args)
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
			res_args->defaults = 1;
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
					"drm",
					"sdl",
					NULL,
				};

		desc = setting_desc_new("Video Backend",
					NULL,
					"[a-z]+",
					DEFAULT_VIDEO,
					values,
					NULL);
		if (!desc)
			return -ENOMEM;

		*next_setting = desc;

		return 1;
	}

	/* XXX: this is kind of hacky for now */
	if (!strcmp(video, "drm")) {
		fb_ops = &drm_fb_ops;

		return drm_fb_ops.setup(settings, next_setting);
	} else if (!strcmp(video, "sdl")) {
		fb_ops = &sdl_fb_ops;

		return sdl_fb_ops.setup(settings, next_setting);
	}

	return -EINVAL;
}

/* select module if not yet selected, then setup the module. */
static int setup_module(settings_t *settings, setting_desc_t **next_setting)
{
	rototiller_module_t	*module;
	const char		*name;

	name = settings_get_key(settings, 0);
	if (!name) {
		const char	*values[nelems(modules) + 1] = {};
		const char	*annotations[nelems(modules) + 1] = {};
		setting_desc_t	*desc;
		unsigned	i;

		for (i = 0; i < nelems(modules); i++) {
			values[i] = modules[i]->name;
			annotations[i] = modules[i]->description;
		}

		desc = setting_desc_new("Renderer Module",
					NULL,
					"[a-zA-Z0-9]+",
					DEFAULT_MODULE,
					values,
					annotations);
		if (!desc)
			return -ENOMEM;

		*next_setting = desc;

		return 1;
	}

	module = module_lookup(name);
	if (!module)
		return -EINVAL;

	/* TODO: here's where the module-specific settings would get hooked */

	return 0;
}

/* turn args into settings, automatically applying defaults if appropriate, or interactively if appropriate. */
/* returns negative value on error, 0 when settings unchanged from args, 1 when changed */
static int setup_from_args(argv_t *args, int defaults, setup_t *res_setup)
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

	r = setup_interactively(setup.module, setup_module, defaults);
	if (r < 0) {
		settings_free(setup.module);
		settings_free(setup.video);

		return r;
	}

	if (r)
		changes = 1;

	r = setup_interactively(setup.video, setup_video, defaults);
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


/* When run with partial/no arguments, if stdin is a tty, enter an interactive setup.
 * If stdin is not a tty, or if --defaults is supplied in argv, default settings are used.
 * If any changes to the settings occur in the course of execution, either interactively or
 * throught --defaults, then print out the explicit CLI invocation usable for reproducing
 * the invocation.
 */
int main(int argc, const char *argv[])
{
	argv_t			args = {};
	setup_t			setup = {};
	void			*context = NULL;
	rototiller_module_t	*module;
	threads_t		*threads;
	fb_t			*fb;
	int			r;

	exit_if(parse_argv(argc, argv, &args) < 0,
		"unable to process arguments");

	if (args.help)
		return print_help() < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

	exit_if((r = setup_from_args(&args, args.defaults, &setup)) < 0,
		"unable to setup");

	exit_if(r && print_setup_as_args(&setup) < 0,
		"unable to print setup");

	exit_if(!(module = module_lookup(settings_get_key(setup.module, 0))),
		"unable to lookup module from settings \"%s\"", settings_get_key(setup.module, 0));

	exit_if(!(fb = fb_new(fb_ops, setup.video, NUM_FB_PAGES)),
		"unable to create fb");

	exit_if(!fps_setup(),
		"unable to setup fps counter");

	exit_if(module->create_context &&
		!(context = module->create_context()),
		"unable to create module context");

	pexit_if(!(threads = threads_create()),
		"unable to create threads");

	for (;;) {
		fb_page_t	*page;

		fps_print(fb);

		page = fb_page_get(fb);
		module_render_page(module, context, threads, page);
		fb_page_put(fb, page);
	}

	threads_destroy(threads);

	if (context)
		module->destroy_context(context);

	fb_free(fb);

	return EXIT_SUCCESS;
}
