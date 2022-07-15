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
#include "til_args.h"
#include "til_settings.h"
#include "til_fb.h"
#include "til_util.h"

#include "fps.h"
#include "setup.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

#define NUM_FB_PAGES	3
/* ^ By triple-buffering, we can have a page tied up being displayed, another
 * tied up submitted and waiting for vsync, and still not block on getting
 * another page so we can begin rendering another frame before vsync.  With
 * just two pages we end up twiddling thumbs until the vsync arrives.
 */
#ifdef HAVE_SDL
#define DEFAULT_VIDEO	"sdl"
#else
#define DEFAULT_VIDEO	"drm"
#endif

extern til_fb_ops_t	drm_fb_ops;
extern til_fb_ops_t	sdl_fb_ops;
static til_fb_ops_t	*fb_ops;

typedef struct rototiller_t {
	const til_module_t	*module;
	til_module_context_t	*module_context;
	pthread_t		thread;
	til_fb_t		*fb;
	struct timeval		start_tv;
	unsigned		ticks_offset;
} rototiller_t;

static rototiller_t		rototiller;


typedef struct setup_t {
	til_settings_t	*module_settings;
	til_setup_t	*module_setup;
	til_settings_t	*video_settings;
	til_setup_t	*video_setup;
} setup_t;

/* FIXME: this is unnecessarily copy-pasta, i think modules should just be made
 * more generic to encompass the setting up uniformly, then basically
 * subclass the video backend vs. renderer stuff.
 */

/* select video backend if not yet selected, then setup the selected backend. */
static int setup_video(til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*setting;
	const char	*video;

	video = til_settings_get_key(settings, 0, &setting);
	if (!video || !setting->desc) {
		til_setting_desc_t	*desc;
		const char		*values[] = {
#ifdef HAVE_DRM
						"drm",
#endif
#ifdef HAVE_SDL
						"sdl",
#endif
						NULL,
					};
		int			r;

		r = til_setting_desc_clone(&(til_setting_desc_t){
						.name = "Video backend",
						.key = NULL,
						.regex = "[a-z]+",
						.preferred = DEFAULT_VIDEO,
						.values = values,
						.annotations = NULL
					}, res_desc);

		if (r < 0)
			return r;

		*res_setting = video ? setting : NULL;

		return 1;
	}

	/* XXX: this is kind of hacky for now */
#ifdef HAVE_DRM
	if (!strcasecmp(video, "drm")) {
		fb_ops = &drm_fb_ops;

		return drm_fb_ops.setup(settings, res_setting, res_desc, res_setup);
	}
#endif
#ifdef HAVE_SDL
	if (!strcasecmp(video, "sdl")) {
		fb_ops = &sdl_fb_ops;

		return sdl_fb_ops.setup(settings, res_setting, res_desc, res_setup);
	}
#endif

	return -EINVAL;
}


/* turn args into settings, automatically applying defaults if appropriate, or interactively if appropriate. */
/* returns negative value on error, 0 when settings unchanged from args, 1 when changed */
/* on error, *res_failed_desc _may_ be assigned with something useful. */
static int setup_from_args(til_args_t *args, setup_t *res_setup, const til_setting_desc_t **res_failed_desc)
{
	int	r = -ENOMEM, changes = 0;
	setup_t	setup = {};

	setup.module_settings = til_settings_new(args->module);
	if (!setup.module_settings)
		goto _err;

	setup.video_settings = til_settings_new(args->video);
	if (!setup.video_settings)
		goto _err;

	r = setup_interactively(setup.module_settings, til_module_setup, args->use_defaults, &setup.module_setup, res_failed_desc);
	if (r < 0)
		goto _err;
	if (r)
		changes = 1;

	r = setup_interactively(setup.video_settings, setup_video, args->use_defaults, &setup.video_setup, res_failed_desc);
	if (r < 0)
		goto _err;
	if (r)
		changes = 1;

	*res_setup = setup;

	return changes;

_err:
	til_settings_free(setup.module_settings);
	til_settings_free(setup.video_settings);

	return r;
}


static int print_setup_as_args(setup_t *setup)
{
	char	*module_args, *video_args;
	char	buf[64];
	int	r;

	module_args = til_settings_as_arg(setup->module_settings);
	if (!module_args) {
		r = -ENOMEM;

		goto _out;
	}

	video_args = til_settings_as_arg(setup->video_settings);
	if (!video_args) {
		r = -ENOMEM;

		goto _out_module;
	}

	r = printf("\nConfigured settings as flags:\n  --module=%s --video=%s\n\nPress enter to continue, add --go to disable this notice...\n",
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
	printf("Run without any flags or partial settings for interactive mode.\n"
		"\n"
		"Supported flags:\n");

	return til_args_help(stdout);
}


static unsigned get_ticks(const struct timeval *start, const struct timeval *now, unsigned offset)
{
	return (unsigned)((now->tv_sec - start->tv_sec) * 1000 + (now->tv_usec - start->tv_usec) / 1000) + offset;
}


static void * rototiller_thread(void *_rt)
{
	rototiller_t	*rt = _rt;
	struct timeval	now;

	for (;;) {
		til_fb_page_t	*page;
		unsigned	ticks;

		page = til_fb_page_get(rt->fb);

		gettimeofday(&now, NULL);
		ticks = get_ticks(&rt->start_tv, &now, rt->ticks_offset);

		til_module_render(rt->module_context, ticks, &page->fragment);

		til_fb_page_put(rt->fb, page);
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
	const til_setting_desc_t	*failed_desc = NULL;
	setup_t				setup = {};
	til_args_t			args = {};
	int				r;

	exit_if((r = til_init()) < 0,
		"unable to initialize libtil: %s", strerror(-r));

	exit_if(til_args_parse(argc, argv, &args) < 0,
		"unable to process arguments");

	if (args.help)
		return print_help() < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

	exit_if((r = setup_from_args(&args, &setup, &failed_desc)) < 0,
		"unable to use args%s%s%s: %s",
		failed_desc ? " for setting \"" : "",
		failed_desc ? failed_desc->key : "",
		failed_desc ? "\"" : "",
		strerror(-r));

	exit_if(!args.gogogo && r && print_setup_as_args(&setup) < 0,
		"unable to print setup");

	exit_if(!(rototiller.module = til_lookup_module(til_settings_get_key(setup.module_settings, 0, NULL))),
		"unable to lookup module from settings \"%s\"", til_settings_get_key(setup.module_settings, 0, NULL));

	exit_if((r = til_fb_new(fb_ops, setup.video_setup, NUM_FB_PAGES, &rototiller.fb)) < 0,
		"unable to create fb: %s", strerror(-r));

	exit_if(!fps_setup(),
		"unable to setup fps counter");

	gettimeofday(&rototiller.start_tv, NULL);
	exit_if((r = til_module_create_context(
						rototiller.module, 0,
						get_ticks(&rototiller.start_tv,
							&rototiller.start_tv,
							rototiller.ticks_offset),
						0,
						setup.module_setup,
						&rototiller.module_context)) < 0,
		"unable to create module context: %s", strerror(-r));

	pexit_if(pthread_create(&rototiller.thread, NULL, rototiller_thread, &rototiller) != 0,
		"unable to create dispatch thread");

	for (;;) {
		if (til_fb_flip(rototiller.fb) < 0)
			break;

		fps_print(rototiller.fb);
	}

	pthread_cancel(rototiller.thread);
	pthread_join(rototiller.thread, NULL);
	til_shutdown();
	til_module_context_free(rototiller.module_context);
	til_fb_free(rototiller.fb);

	return EXIT_SUCCESS;
}
