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
#include "til_fb.h"
#include "til_settings.h"
#include "til_stream.h"
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

#ifndef DEFAULT_VIDEO
#ifdef HAVE_SDL
#define DEFAULT_VIDEO	"sdl"
#endif
#endif

#ifndef DEFAULT_VIDEO
#ifdef  HAVE_DRM
#define DEFAULT_VIDEO	"drm"
#endif
#endif

#ifndef DEFAULT_VIDEO
#define DEFAULT_VIDEO	"mem"
#endif

extern til_fb_ops_t	drm_fb_ops;
extern til_fb_ops_t	mem_fb_ops;
extern til_fb_ops_t	sdl_fb_ops;
static til_fb_ops_t	*fb_ops;

typedef struct rototiller_t {
	til_args_t		args;
	const til_module_t	*module;
	til_module_context_t	*module_context;
	til_stream_t		*stream;
	til_fb_fragment_t	*fragment;
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
	unsigned	seed;
	const char	*title;
} setup_t;

/* FIXME: this is unnecessarily copy-pasta, i think modules should just be made
 * more generic to encompass the setting up uniformly, then basically
 * subclass the video backend vs. renderer stuff.
 */

/* select video backend if not yet selected, then setup the selected backend. */
static int setup_video(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*setting;
	const char	*video;

	video = til_settings_get_value_by_idx(settings, 0, &setting);
	if (!video || !setting->desc) {
		const char		*values[] = {
#ifdef HAVE_DRM
						"drm",
#endif
						"mem",
#ifdef HAVE_SDL
						"sdl",
#endif
						NULL,
					};
		int			r;

		r = til_setting_desc_new(	settings,
						&(til_setting_spec_t){
							.name = "Video backend",
							.key = NULL,
							.regex = "[a-z]+",
							.preferred = DEFAULT_VIDEO,
							.values = values,
							.annotations = NULL,
							.as_label = 1,
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
	if (!strcasecmp(video, "mem")) {
		fb_ops = &mem_fb_ops;

		return mem_fb_ops.setup(settings, res_setting, res_desc, res_setup);
	}
#ifdef HAVE_SDL
	if (!strcasecmp(video, "sdl")) {
		fb_ops = &sdl_fb_ops;

		return sdl_fb_ops.setup(settings, res_setting, res_desc, res_setup);
	}
#endif

	return -EINVAL;
}


/* TODO: move to til.c */
/* parse a hexadecimal seed with an optional leading 0x prefix into a libc srand()-appropriate machine-dependent sized unsigned int */
/* returns -errno on any failure (including overflow), 0 on success. */
static int parse_seed(const char *in, unsigned *res_seed)
{
	unsigned	seed = 0;

	assert(in);
	assert(res_seed);

	if (in[0] == '0' && (in[1] == 'x' || in[1] == 'X')) /* accept and ignore leading "0[xX]" */
		in += 2;

	for (int i = 0; *in && i < sizeof(*res_seed) * 2;) {
		uint8_t	h = 0;

		seed <<= 8;

		for (int j = 0; *in && j < 2; in++, j++, i++) {
			h <<= 4;

			switch (*in) {
			case '0'...'9':
				h |= (*in) - '0';
				break;

			case 'a'...'f':
				h |= (*in) - 'a' + 10;
				break;

			case 'A'...'F':
				h |= (*in) - 'A' + 10;
				break;

			default:
				return -EINVAL;
			}
		}

		seed |= h;
	}

	if (*in)
		return -EOVERFLOW;

	*res_seed = seed;

	return 0;
}


/* TODO: move to til.c, setup_t in general should just become til_setup_t.
 * the sticking point is setup_interactively() is very rototiller-specific, so it needs
 * to be turned into a caller-supplied callback or something.
 */
/* turn args into settings, automatically applying defaults if appropriate, or interactively if appropriate. */
/* returns negative value on error, 0 when settings unchanged from args, 1 when changed */
/* on error, *res_failed_desc _may_ be assigned with something useful. */
static int setup_from_args(til_args_t *args, setup_t *res_setup, const char **res_failed_desc_path)
{
	int	r = -ENOMEM, changes = 0;
	setup_t	setup = { .seed = time(NULL) + getpid() };

	assert(args);
	assert(res_setup);

	setup.title = strdup(args->title ? : "rototiller");
	if (!setup.title)
		goto _err;

	if (args->seed) {
		r = parse_seed(args->seed, &setup.seed);
		if (r < 0)
			goto _err;
	}

	/* FIXME TODO: this is gross! but we want to seed the PRNG before we do any actual setup
	 * in case we're randomizing settings.
	 * Maybe it makes more sense to just add a TIL_SEED env variable and let til_init() getenv("TIL_SEED")
	 * and do all this instead of setup_from_args().  This'll do for now.
	 */
	srand(setup.seed);

	setup.module_settings = til_settings_new(NULL, NULL, "module", args->module);
	if (!setup.module_settings)
		goto _err;

	setup.video_settings = til_settings_new(NULL, NULL, "video", args->video);
	if (!setup.video_settings)
		goto _err;

	r = setup_interactively(setup.module_settings, til_module_setup, args->use_defaults, &setup.module_setup, res_failed_desc_path);
	if (r < 0)
		goto _err;
	if (r)
		changes = 1;

	r = setup_interactively(setup.video_settings, setup_video, args->use_defaults, &setup.video_setup, res_failed_desc_path);
	if (r < 0)
		goto _err;
	if (r)
		changes = 1;

	*res_setup = setup;

	return changes;

_err:
	free((void *)setup.title);
	til_settings_free(setup.module_settings);
	til_settings_free(setup.video_settings);

	return r;
}


static char * seed_as_arg(unsigned seed)
{
	char	arg[sizeof("0x") + sizeof(seed) * 2];

	snprintf(arg, sizeof(arg), "0x%x", seed);

	return strdup(arg);
}


static int print_setup_as_args(setup_t *setup, int wait)
{
	char	*seed_arg, *module_args, *video_args;
	char	buf[64];
	int	r = -ENOMEM;

	seed_arg = seed_as_arg(setup->seed);
	if (!seed_arg)
		goto _out;

	module_args = til_settings_as_arg(setup->module_settings);
	if (!module_args)
		goto _out_seed;

	video_args = til_settings_as_arg(setup->video_settings);
	if (!video_args)
		goto _out_module;

	r = printf("\nConfigured settings as flags:\n  --seed=%s '--module=%s' '--video=%s'\n",
		seed_arg,
		module_args,
		video_args);
	if (r < 0)
		goto _out_video;

	if (wait) {
		r = printf("\nPress enter to continue, add --go to skip this step...\n");
		if (r < 0)
			goto _out_video;

		(void) fgets(buf, sizeof(buf), stdin);
	}

_out_video:
	free(video_args);
_out_module:
	free(module_args);
_out_seed:
	free(seed_arg);
_out:
	return r;
}


static int print_help(void)
{
	printf("\nRun without any flags or partial settings for interactive mode.\n"
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
		unsigned		ticks;

		rt->fragment = til_fb_page_get(rt->fb);
		gettimeofday(&now, NULL);
		ticks = get_ticks(&rt->start_tv, &now, rt->ticks_offset);
		til_module_render(rt->module_context, rt->stream, ticks, &rt->fragment);
		til_fb_fragment_submit(rt->fragment);

		if (rt->args.print_module_contexts || rt->args.print_pipes) {
			/* render threads are idle at this point */
			printf("\x1b[2J\x1b[;H"); /* ANSI codes for clear screen and move cursor to top left */

			if (rt->args.print_module_contexts)
				til_stream_fprint_module_contexts(rt->stream, stdout);

			if (rt->args.print_pipes)
				til_stream_fprint_pipes(rt->stream, stdout);
		}
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
	const char	*failed_desc_path = NULL;
	setup_t		setup = {};
	int		r;

	exit_if((r = til_init()) < 0,
		"unable to initialize libtil: %s", strerror(-r));

	exit_if(til_args_parse(argc, argv, &rototiller.args) < 0,
		"unable to process arguments");

	if (rototiller.args.help)
		return print_help() < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

	exit_if((r = setup_from_args(&rototiller.args, &setup, &failed_desc_path)) < 0,
		"unable to use args%s%s%s: %s",
		failed_desc_path ? " for setting \"" : "",
		failed_desc_path ? : "",
		failed_desc_path ? "\"" : "", /* XXX: technically leaking the path when set, oh well */
		strerror(-r));

	exit_if(r && print_setup_as_args(&setup, !rototiller.args.gogogo) < 0,
		"unable to print setup");

	if (setup.module_setup) { /* the "none" builtin produces a NULL setup successfully */
		exit_if(!(rototiller.module = til_lookup_module(til_settings_get_value_by_idx(setup.module_settings, 0, NULL))),
			"unable to lookup module from settings \"%s\"", til_settings_get_value_by_idx(setup.module_settings, 0, NULL));

		exit_if((r = til_fb_new(fb_ops, setup.title, setup.video_setup, NUM_FB_PAGES, &rototiller.fb)) < 0,
			"unable to create fb: %s", strerror(-r));

		exit_if(!(rototiller.stream = til_stream_new()),
			"unable to create root stream");

		exit_if(!fps_setup(),
			"unable to setup fps counter");

		gettimeofday(&rototiller.start_tv, NULL);
		exit_if((r = til_module_create_context(	rototiller.module,
							rototiller.stream,
							setup.seed,
							get_ticks(&rototiller.start_tv,
								&rototiller.start_tv,
								rototiller.ticks_offset),
							0,
							setup.module_setup,
							&rototiller.module_context)) < 0,
			"unable to create module context: %s", strerror(-r));

		pexit_if(pthread_create(&rototiller.thread, NULL, rototiller_thread, &rototiller) != 0,
			"unable to create dispatch thread");

		while (til_stream_active(rototiller.stream)) {
			if (til_fb_flip(rototiller.fb) < 0)
				break;

			fps_fprint(rototiller.fb, stderr);
		}

		pthread_cancel(rototiller.thread);
		pthread_join(rototiller.thread, NULL);
		til_quiesce();

		til_module_context_free(rototiller.module_context);
		til_stream_free(rototiller.stream);
		til_fb_free(rototiller.fb);
	}

	til_shutdown();

	return EXIT_SUCCESS;
}
