#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rocket/rocket/lib/device.h"
#include "rocket/rocket/lib/sync.h"
#include "rocket/rocket/lib/track.h"

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_stream.h"
#include "til_tap.h"
#include "til_util.h"

#include "txt/txt.h"

/* Copyright (C) 2023 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements a rudimentary sequencing module varying
 * "tapped" variables of other modules on a timeline via
 * GNU Rocket (https://github.com/rocket/rocket)
 */

typedef struct rkt_scene_t {
	const til_module_t	*module;
	til_module_context_t	*module_ctxt;
} rkt_scene_t;

typedef struct rkt_context_t {
	til_module_context_t	til_module_context;

	struct sync_device	*sync_device;
	const struct sync_track	*scene_track;
	double			rows_per_ms;
	double			rocket_row;
	unsigned		last_ticks;
	unsigned		paused:1;
	rkt_scene_t		scenes[];
} rkt_context_t;

typedef struct rkt_setup_scene_t {
	char			*module_name;
	til_setup_t		*setup;
} rkt_setup_scene_t;

typedef struct rkt_setup_t {
	til_setup_t		til_setup;
	const char		*base;
	double			rows_per_ms;
	unsigned		connect:1;
	const char		*host;
	unsigned short		port;
	size_t			n_scenes;
	rkt_setup_scene_t	scenes[];
} rkt_setup_t;


/* variadic helper wrapping librocket's sync_get_track() */
static const struct sync_track * sync_get_trackf(struct sync_device *device, const char *format, ...)
{
	char	buf[4096];
	size_t	len;
	va_list	ap;

	assert(device);
	assert(format);

	va_start(ap, format);
	len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	if (len >= sizeof(buf))
		return NULL;

	return sync_get_track(device, buf);
}


static til_module_context_t * rkt_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	rkt_setup_t	*s = (rkt_setup_t *)setup;
	rkt_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(rkt_context_t) + s->n_scenes * sizeof(*ctxt->scenes), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->sync_device = sync_create_device(s->base);
	if (!ctxt->sync_device)
		return til_module_context_free(&ctxt->til_module_context);

	if (s->connect) {
		/* XXX: it'd be better if we just reconnected periodically instead of hard failing */
		if (sync_tcp_connect(ctxt->sync_device, s->host, s->port))
			return til_module_context_free(&ctxt->til_module_context);
	}

	ctxt->scene_track = sync_get_trackf(ctxt->sync_device, "%s:scene", setup->path);
	if (!ctxt->scene_track)
		return til_module_context_free(&ctxt->til_module_context);

	for (size_t i = 0; i < s->n_scenes; i++) {
		int		r;

		/* FIXME TODO: this needs to be handle-aware so scenes can directly reference existing contexts */
		ctxt->scenes[i].module = til_lookup_module(s->scenes[i].module_name);
		if (!ctxt->scenes[i].module) /* this isn't really expected since setup already does this */
			return til_module_context_free(&ctxt->til_module_context);

		r = til_module_create_context(ctxt->scenes[i].module, stream, rand_r(&seed), ticks, 0, s->scenes[i].setup, &ctxt->scenes[i].module_ctxt);
		if (r < 0)
			return til_module_context_free(&ctxt->til_module_context);
	}

	ctxt->rows_per_ms = s->rows_per_ms;
	ctxt->last_ticks = ticks;

	return &ctxt->til_module_context;
}


static void rkt_destroy_context(til_module_context_t *context)
{
	rkt_context_t	*ctxt = (rkt_context_t *)context;

	if (ctxt->sync_device)
		sync_destroy_device(ctxt->sync_device);

	for (size_t i = 0; i < ((rkt_setup_t *)context->setup)->n_scenes; i++)
		til_module_context_free(ctxt->scenes[i].module_ctxt);

	free(context);
}


static void rkt_sync_pause(void *context, int flag)
{
	rkt_context_t	*ctxt = context;

	if (flag)
		ctxt->paused = 1;
	else
		ctxt->paused = 0;
}


static void rkt_sync_set_row(void *context, int row)
{
	rkt_context_t	*ctxt = context;

	ctxt->rocket_row = row;
}


static int rkt_sync_is_playing(void *context)
{
	rkt_context_t	*ctxt = context;

	/* returns bool, 1 for is playing */
	return !ctxt->paused;
}


static struct sync_cb rkt_sync_cb = {
	rkt_sync_pause,
	rkt_sync_set_row,
	rkt_sync_is_playing,
};


typedef struct rkt_pipe_t {
	/* rocket basically only applies to floats, so we only need a float, its tap, and a sync track */
	til_tap_t		tap;

	union {
		float	f;
		double	d;
	} var;

	union {
		float	*f;
		double	*d;
	} ptr;

	const struct sync_track	*track;
} rkt_pipe_t;


int rkt_stream_pipe_ctor(void *context, til_stream_t *stream, const void *owner, const void *owner_foo, const char *parent_path, uint32_t parent_hash, const til_tap_t *tap, const void **res_owner, const void **res_owner_foo, const til_tap_t **res_driving_tap)
{
	rkt_context_t	*ctxt = context;
	rkt_pipe_t	*rkt_pipe;

	assert(stream);
	assert(tap);
	assert(res_owner);
	assert(res_owner_foo);
	assert(res_driving_tap);

	if (tap->type != TIL_TAP_TYPE_FLOAT &&
	    tap->type != TIL_TAP_TYPE_DOUBLE)
		return 0;	/* not interesting to us */

	/* assume pipe ownership, create driving tap and rocket track to stow @ owner_foo */

	rkt_pipe = calloc(1, sizeof(rkt_pipe_t));
	if (!rkt_pipe)
		return -ENOMEM;

	rkt_pipe->tap = til_tap_init(ctxt, tap->type, &rkt_pipe->ptr, 1, &rkt_pipe->var, tap->name);
	rkt_pipe->track = sync_get_trackf(ctxt->sync_device, "%s:%s", parent_path, tap->name);

	*res_owner = ctxt;
	*res_owner_foo = rkt_pipe;
	*res_driving_tap = rkt_pipe->track->num_keys ? &rkt_pipe->tap : tap;

	return 1;
}


static const til_stream_hooks_t	rkt_stream_hooks = {
	.pipe_ctor = rkt_stream_pipe_ctor,
	/* .pipe_dtor unneeded */
};


static int rkt_pipe_update(void *context, til_stream_pipe_t *pipe, const void *owner, const void *owner_foo, const til_tap_t *driving_tap)
{
	rkt_pipe_t	*rkt_pipe = (rkt_pipe_t *)owner_foo;
	rkt_context_t	*ctxt = context;
	double		val;

	/* just ignore pipes we don't own (they're not types we can drive w/rocket) */
	if (owner != ctxt)
		return 0;

	/* when there's no keys in the track, flag as inactive so someone else can drive */
	if (!rkt_pipe->track->num_keys) {
		rkt_pipe->tap.inactive = 1;

		return 0;
	}

	rkt_pipe->tap.inactive = 0;
	if (driving_tap != &rkt_pipe->tap)
		til_stream_pipe_set_driving_tap(pipe, &rkt_pipe->tap);

	/* otherwise get the current interpolated value from the rocket track @ owner_foo->track
	 * to update owner_foo->var.[fd], which _should_ be the driving tap.
	 */
	val = sync_get_val(rkt_pipe->track, ctxt->rocket_row);
	switch (rkt_pipe->tap.type) {
	case TIL_TAP_TYPE_FLOAT:
		rkt_pipe->var.f = val;
		break;
	case TIL_TAP_TYPE_DOUBLE:
		rkt_pipe->var.d = val;
		break;
	default:
		assert(0);
	}

	return 0;
}


static void rkt_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	rkt_context_t	*ctxt = (rkt_context_t *)context;

	if (!ctxt->paused)
		ctxt->rocket_row += ((double)(ticks - ctxt->last_ticks)) * ctxt->rows_per_ms;

	ctxt->last_ticks = ticks;

	/* hooks-setting is idempotent and cheap so we just always do it, and technicallly the stream can get changed out on us frame-to-frame */
	til_stream_set_hooks(stream, &rkt_stream_hooks, ctxt);

	/* ctxt->rocket_row needs to be updated */
	sync_update(ctxt->sync_device, ctxt->rocket_row, &rkt_sync_cb, ctxt);

	/* this drives our per-rocket-track updates, with the tracks registered as owner_foo on the pipes, respectively */
	til_stream_for_each_pipe(stream, rkt_pipe_update, ctxt);

	{
		unsigned	scene;

		scene = (unsigned)sync_get_val(ctxt->scene_track, ctxt->rocket_row);
		if (scene < ((rkt_setup_t *)context->setup)->n_scenes)
			til_module_render(ctxt->scenes[scene].module_ctxt, stream, ticks, fragment_ptr);
		else {
			txt_t	*msg = txt_newf("%s: NO SCENE @ %u", context->setup->path, scene);

			/* TODO: creating/destroying this every frame is dumb, but
			 * as this is a diagnostic it's not so important.
			 *
			 * Once this module deals with disconnects and transparently reconnects, it'll need
			 * to show some connection status information as well... when that gets added this will
			 * likely get reworked to become part of that status text.
			 */
			til_fb_fragment_clear(*fragment_ptr);
			txt_render_fragment(msg, *fragment_ptr, 0xffffffff,
					    0, 0,
					    (txt_align_t){
						.horiz = TXT_HALIGN_LEFT,
						.vert = TXT_VALIGN_TOP,
					   });
			txt_free(msg);
		}
	}
}


static void rkt_setup_free(til_setup_t *setup)
{
	rkt_setup_t	*s = (rkt_setup_t *)setup;

	if (s) {
		for (size_t i = 0; i < s->n_scenes; i++) {
			free(s->scenes[i].module_name);
			til_setup_free(s->scenes[i].setup);
		}
		free((void *)s->base);
		free((void *)s->host);
		free(setup);
	}
}


static int rkt_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const til_settings_t	*scenes_settings;
	const char		*connect_values[] = {
					"off",
					"on",
					NULL
				};
	const char		*scenes;
	const char		*base;
	const char		*bpm;
	const char		*rpb;
	const char		*connect;
	const char		*host;
	const char		*port;
	int			r;

	/* This is largely taken from compose::layers, but might just go away when I add tables to rocket,
	 * or maybe they can coexist.
	 */
	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Comma-separated list of modules for scenes to sequence",
							.key = "scenes",
							.preferred = "compose,compose,compose,compose",
							.annotations = NULL,
							.as_nested_settings = 1,
						},
						&scenes,
						res_setting,
						res_desc);
	if (r)
		return r;

	assert(res_setting && *res_setting && (*res_setting)->value_as_nested_settings);
	scenes_settings = (*res_setting)->value_as_nested_settings;
	{
		til_setting_t	*scene_setting;

		for (size_t i = 0; til_settings_get_value_by_idx(scenes_settings, i, &scene_setting); i++) {
			if (!scene_setting->value_as_nested_settings) {
				r = til_setting_desc_new(	scenes_settings,
								&(til_setting_spec_t){
									.as_nested_settings = 1,
								}, res_desc);
				if (r < 0)
					return r;

				*res_setting = scene_setting;

				return 1;
			}
		}

		for (size_t i = 0; til_settings_get_value_by_idx(scenes_settings, i, &scene_setting); i++) {
			til_setting_t		*scene_module_setting;
			const char		*scene_module_name = til_settings_get_value_by_idx(scene_setting->value_as_nested_settings, 0, &scene_module_setting);
			const til_module_t	*scene_module = til_lookup_module(scene_module_name);

			if (!scene_module || !scene_module_setting)
				return -EINVAL;

			if (!scene_module_setting->desc) {
				r = til_setting_desc_new(	scene_setting->value_as_nested_settings,
								&(til_setting_spec_t){
									.name = "Scene module name",
									.preferred = "none",
									.as_label = 1,
								}, res_desc);
				if (r < 0)
					return r;

				*res_setting = scene_module_setting;

				return 1;
			}

			if (scene_module->setup) {
				r = scene_module->setup(scene_setting->value_as_nested_settings, res_setting, res_desc, NULL);
				if (r)
					return r;
			}
		}
	}

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Rocket \"base\" label",
							.key = "base",
							.preferred = "tiller",
							.annotations = NULL,
						},
						&base,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Beats per minute",
							.key = "bpm",
							.preferred = "125",
							.annotations = NULL,
						},
						&bpm,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Rows per beat",
							.key = "rpb",
							.preferred = "8",
							.annotations = NULL,
						},
						&rpb,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Editor connection toggle",
							.key = "connect",
							/* TODO: regex */
							.preferred = connect_values[1],
							.values = connect_values,
							.annotations = NULL,
						},
						&connect,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (!strcasecmp(connect, "on")) {
		r = til_settings_get_and_describe_value(settings,
							&(til_setting_spec_t){
								.name = "Editor host",
								.key = "host",
								.preferred = "localhost",
								/* TODO: regex */
								.annotations = NULL,
							},
							&host,
							res_setting,
							res_desc);
		if (r)
			return r;

		r = til_settings_get_and_describe_value(settings,
							&(til_setting_spec_t){
								.name = "Editor port",
								.key = "port",
								.preferred = TIL_SETTINGS_STR(SYNC_DEFAULT_PORT),
								/* TODO: regex */
								.annotations = NULL,
							},
							&port,
							res_setting,
							res_desc);
		if (r)
			return r;
	}

	if (res_setup) {
		size_t			n_scenes = til_settings_get_count(scenes_settings);
		til_setting_t		*scene_setting;
		rkt_setup_t		*setup;
		unsigned		ibpm, irpb;

		setup = til_setup_new(settings, sizeof(*setup) + n_scenes * sizeof(*setup->scenes), rkt_setup_free);
		if (!setup)
			return -ENOMEM;

		setup->n_scenes = n_scenes;

		for (size_t i = 0; til_settings_get_value_by_idx(scenes_settings, i, &scene_setting); i++) {
			const char		*scene_module_name = til_settings_get_value_by_idx(scene_setting->value_as_nested_settings, 0, NULL);
			const til_module_t	*scene_module = til_lookup_module(scene_module_name);

			if (!scene_module || !strcmp(scene_module_name, "rkt")) {
				til_setup_free(&setup->til_setup);

				return -EINVAL;
			}

			/* XXX If it's appropriate stow the resolved til_module_t* or the name is still unclear, since
			 * the module names will soon be able to address existing contexts in the stream at their path.
			 * So for now I'm just going to continue stowing the name, even though the lookup above prevents
			 * any sort of context address being used...
			 */
			setup->scenes[i].module_name = strdup(scene_module_name);
			if (!setup->scenes[i].module_name) {
				til_setup_free(&setup->til_setup);

				return -ENOMEM;
			}

			r = til_module_setup_finalize(scene_module, scene_setting->value_as_nested_settings, &setup->scenes[i].setup);
			if (r < 0) {
				til_setup_free(&setup->til_setup);

				return r;
			}
		}

		setup->base = strdup(base);
		if (!setup->base) {
			til_setup_free(&setup->til_setup);

			return -ENOMEM;
		}

		if (!strcasecmp(connect, "on")) {
			setup->connect = 1;

			setup->host = strdup(host);
			if (!setup->host) {
				til_setup_free(&setup->til_setup);

				return -ENOMEM;
			}

			sscanf(port, "%hu", &setup->port); /* FIXME parse errors */
		}

		sscanf(bpm, "%u", &ibpm);
		sscanf(rpb, "%u", &irpb);
		setup->rows_per_ms = ((double)(ibpm * irpb)) * (1.0 / (60.0 * 1000.0));

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	rkt_module = {
	.create_context = rkt_create_context,
	.destroy_context = rkt_destroy_context,
	.render_fragment = rkt_render_fragment,
	.name = "rkt",
	.description = "GNU Rocket module sequencer",
	.setup = rkt_setup,
	.flags = TIL_MODULE_HERMETIC | TIL_MODULE_EXPERIMENTAL,	/* this needs refinement esp. if rkt gets split into a player and editor */
};
