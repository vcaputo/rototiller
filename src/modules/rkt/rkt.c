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

typedef struct rkt_context_t {
	til_module_context_t	til_module_context;

	const til_module_t	*seq_module;
	til_module_context_t	*seq_module_ctxt;

	struct sync_device	*sync_device;
	double			rows_per_ms;
	double			rocket_row;
	unsigned		last_ticks;
	unsigned		paused:1;
} rkt_context_t;

typedef struct rkt_setup_t {
	til_setup_t		til_setup;
	const char		*seq_module_name;
	const char		*base;
	double			rows_per_ms;
	unsigned		connect:1;
	const char		*host;
	unsigned short		port;
} rkt_setup_t;


static til_module_context_t * rkt_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	rkt_context_t	*ctxt;
	const til_module_t	*seq_module;

	seq_module = til_lookup_module(((rkt_setup_t *)setup)->seq_module_name);
	if (!seq_module)
		return NULL;

	ctxt = til_module_context_new(module, sizeof(rkt_context_t), stream, seed, ticks, n_cpus, path, setup);
	if (!ctxt)
		return NULL;

	ctxt->sync_device = sync_create_device(((rkt_setup_t *)setup)->base);
	if (!ctxt->sync_device)
		return til_module_context_free(&ctxt->til_module_context);

	if (((rkt_setup_t *)setup)->connect) {
		/* XXX: it'd be better if we just reconnected periodically instead of hard failing */
		if (sync_tcp_connect(ctxt->sync_device, ((rkt_setup_t *)setup)->host, ((rkt_setup_t *)setup)->port))
			return til_module_context_free(&ctxt->til_module_context);
	}

	ctxt->seq_module = seq_module;

	{
		til_setup_t	*module_setup = NULL;

		(void) til_module_randomize_setup(ctxt->seq_module, rand_r(&seed), &module_setup, NULL);

		(void) til_module_create_context(ctxt->seq_module, stream, rand_r(&seed), ticks, 0, path, module_setup, &ctxt->seq_module_ctxt);
		til_setup_free(module_setup);
	}

	ctxt->rows_per_ms = ((rkt_setup_t *)setup)->rows_per_ms;
	ctxt->last_ticks = ticks;

	return &ctxt->til_module_context;
}


static void rkt_destroy_context(til_module_context_t *context)
{
	rkt_context_t	*ctxt = (rkt_context_t *)context;

	if (ctxt->sync_device)
		sync_destroy_device(ctxt->sync_device);
	til_module_context_free(ctxt->seq_module_ctxt);
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
	char			track_name[];
} rkt_pipe_t;


int rkt_stream_pipe_ctor(void *context, til_stream_t *stream, const void *owner, const void *owner_foo, const char *parent_path, uint32_t parent_hash, const til_tap_t *tap, const void **res_owner, const void **res_owner_foo, const til_tap_t **res_driving_tap)
{
	rkt_context_t	*ctxt = context;
	rkt_pipe_t		*rkt_pipe;
	size_t			track_name_len;

	assert(stream);
	assert(tap);
	assert(res_owner);
	assert(res_owner_foo);
	assert(res_driving_tap);

	if (tap->type != TIL_TAP_TYPE_FLOAT &&
	    tap->type != TIL_TAP_TYPE_DOUBLE)
		return 0;	/* not interesting to us */

	/* we take ownership, and create our own tap and rocket track to stow @ owner_foo */

	/* rocket has its own syntax for track names so instead of consttructing a concatenated path
	 * in til_stream_pipe_t and passing it to the ctor, just construct our own in the end of rkt_pipe_t
	 */
	track_name_len = strlen(parent_path) + 1 + strlen(tap->name) + 1;
	rkt_pipe = calloc(1, sizeof(rkt_pipe_t) + track_name_len);
	if (!rkt_pipe)
		return -ENOMEM;

	snprintf(rkt_pipe->track_name, track_name_len, "%s:%s", parent_path, tap->name);
	rkt_pipe->tap = til_tap_init(ctxt, tap->type, &rkt_pipe->ptr, 1, &rkt_pipe->var, tap->name);
	rkt_pipe->track = sync_get_track(ctxt->sync_device, rkt_pipe->track_name);

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
	rkt_pipe_t		*rkt_pipe = (rkt_pipe_t *)owner_foo;
	rkt_context_t	*ctxt = context;
	double			val;

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

	til_module_render(ctxt->seq_module_ctxt, stream, ticks, fragment_ptr);
}


static int rkt_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*connect_values[] = {
				"off",
				"on",
				NULL
			};
	const char	*seq_module;
	const char	*base;
	const char	*bpm;
	const char	*rpb;
	const char	*connect;
	const char	*host;
	const char	*port;
	int		r;

	/* TODO:
	 * Instead of driving a single module, we could accept a list of module specifiers
	 * including settings for each (requiring the recursive settings support to land).
	 * Then just use a module selector track for switching between the modules... that
	 * might work for getting full-blown demos sequenced via rocket.
	 */
	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Module to sequence",
							.key = "seq_module",
							.preferred = "compose",
							.annotations = NULL,
						},
						&seq_module,
						res_setting,
						res_desc);
	if (r)
		return r;

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
		const til_module_t	*til_seq_module;
		rkt_setup_t		*setup;
		unsigned		ibpm, irpb;

		if (!strcmp(seq_module, "rkt"))
			return -EINVAL;

		til_seq_module = til_lookup_module(seq_module);
		if (!til_seq_module)
			return -ENOENT;

		/* TODO: we're going to need a custom setup_free to cleanup host+base etc. */
		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		setup->seq_module_name = til_seq_module->name;
		setup->base = strdup(base); /* FIXME errors */
		if (!strcasecmp(connect, "on")) {
			setup->connect = 1;
			setup->host = strdup(host); /* FIXME errors */
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
