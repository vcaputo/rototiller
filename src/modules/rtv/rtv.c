#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_util.h"

#include "txt/txt.h"

/* Copyright (C) 2019 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements an MTV-inspired random slideshow of rototiller modules.
 *
 * Some TODO items:
 * - optionally persist module contexts so they resume rather than restart
 */

#define RTV_SNOW_DURATION_SECS		0
#define RTV_DURATION_SECS		4
#define RTV_CONTEXT_DURATION_SECS	4
#define RTV_CAPTION_DURATION_SECS	2
#define RTV_DEFAULT_SNOW_MODULE		"none"
#define RTV_DEFAULT_LOG_SETTINGS	0

typedef struct rtv_channel_t {
	const til_module_t	*module;
	til_module_context_t	*module_ctxt;
	til_setup_t		*module_setup;
	time_t			last_on_time, cumulative_time;
	char			*settings_as_arg;
	txt_t			*caption;
	unsigned		order;
} rtv_channel_t;

typedef struct rtv_context_t {
	til_module_context_t	til_module_context;
	time_t			next_switch, next_hide_caption;
	rtv_channel_t		*channel, *last_channel;
	txt_t			*caption;

	unsigned		duration;
	unsigned		context_duration;
	unsigned		snow_duration;
	unsigned		caption_duration;
	unsigned		log_channels:1;

	rtv_channel_t		snow_channel;

	size_t			n_channels;
	rtv_channel_t		channels[];
} rtv_context_t;

typedef struct rtv_setup_t {
	til_setup_t		til_setup;
	unsigned		duration;
	unsigned		context_duration;
	unsigned		snow_duration;
	unsigned		caption_duration;
	char			*snow_module;
	unsigned		log_channels:1;
	char			*channels[];
} rtv_setup_t;

static void setup_next_channel(rtv_context_t *ctxt, unsigned ticks);
static til_module_context_t * rtv_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup);
static void rtv_destroy_context(til_module_context_t *context);
static void rtv_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr);
static void rtv_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr);
static int rtv_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);

static til_module_t	rtv_none_module = {};

til_module_t	rtv_module = {
	.create_context = rtv_create_context,
	.destroy_context = rtv_destroy_context,
	.render_fragment = rtv_render_fragment,
	.finish_frame = rtv_finish_frame,
	.name = "rtv",
	.description = "Rototiller TV",
	.setup = rtv_setup,
};


static int cmp_channels(const void *p1, const void *p2)
{
	const rtv_channel_t	*c1 = p1, *c2 = p2;

	if (c1->order < c2->order)
		return -1;

	if (c1->order > c2->order)
		return 1;

	return 0;
}


static void randomize_channels(rtv_context_t *ctxt)
{
	for (size_t i = 0; i < ctxt->n_channels; i++)
		ctxt->channels[i].order = rand_r(&ctxt->til_module_context.seed);

	qsort(ctxt->channels, ctxt->n_channels, sizeof(rtv_channel_t), cmp_channels);
}


static void cleanup_channel(rtv_context_t *ctxt)
{
	if (!ctxt->channel)
		return;

	ctxt->channel->cumulative_time = 0;

	ctxt->channel->module_ctxt = til_module_context_free(ctxt->channel->module_ctxt);

	free(ctxt->channel->settings_as_arg);
	ctxt->channel->settings_as_arg = NULL;

	ctxt->caption = ctxt->channel->caption = txt_free(ctxt->channel->caption);
}


static void setup_next_channel(rtv_context_t *ctxt, unsigned ticks)
{
	time_t	now = time(NULL);

	/* TODO: most of this module stuff should probably be
	 * in rototiller.c helpers, but it's harmless for now.
	 */
	if (ctxt->channel) {
		ctxt->channel->cumulative_time += now - ctxt->channel->last_on_time;
		if (ctxt->channel->cumulative_time >= ctxt->context_duration)
			cleanup_channel(ctxt);
	}

	if (!ctxt->n_channels ||
	    (ctxt->channel != &ctxt->snow_channel && ctxt->snow_channel.module != &rtv_none_module)) {

		ctxt->last_channel = ctxt->channel;
		ctxt->channel = &ctxt->snow_channel;
		ctxt->caption = NULL;
		ctxt->next_switch = now + ctxt->snow_duration;
	} else {
		size_t	i;

		for (i = 0; i < ctxt->n_channels; i++) {
			if (&ctxt->channels[i] == ctxt->last_channel) {
				i++;
				break;
			}
		}

		if (i >= ctxt->n_channels) {
			randomize_channels(ctxt);
			ctxt->last_channel = NULL;
			i = 0;
		}

		ctxt->channel = &ctxt->channels[i];

		if (!ctxt->channel->settings_as_arg) {
			char	*settings_as_arg = NULL;
			txt_t	*caption;

			(void) til_module_randomize_setup(ctxt->channel->module, rand_r(&ctxt->til_module_context.seed), &ctxt->channel->module_setup, &settings_as_arg);
			caption = txt_newf("Title: %s%s%s\nDescription: %s%s%s",
						 ctxt->channel->module->name,
						 ctxt->channel->module->author ? "\nAuthor: " : "",
						 ctxt->channel->module->author ? : "",
						 ctxt->channel->module->description,
						 settings_as_arg ? "\nSettings: " : "",
						 settings_as_arg ? settings_as_arg : "");

			ctxt->caption = ctxt->channel->caption = caption;
			ctxt->channel->settings_as_arg = settings_as_arg ? settings_as_arg : strdup("");

			if (ctxt->log_channels) /* TODO: we need to capture seed state too, a general solution capturing such global state would be nice */
				fprintf(stderr, "rtv channel settings: \'%s\'\n", ctxt->channel->settings_as_arg);
		}

		ctxt->next_switch = now + ctxt->duration;
		ctxt->next_hide_caption = now + ctxt->caption_duration;
	}

	if (!ctxt->channel->module_ctxt)
		(void) til_module_create_context(ctxt->channel->module, ctxt->til_module_context.stream, rand_r(&ctxt->til_module_context.seed), ticks, 0, ctxt->til_module_context.path, ctxt->channel->module_setup, &ctxt->channel->module_ctxt);

	ctxt->channel->last_on_time = now;
}


static int rtv_should_skip_module(const rtv_setup_t *setup, const til_module_t *module)
{
	if (module == &rtv_module ||
	    (setup->snow_module && !strcasecmp(module->name, setup->snow_module)))
		return 1;

	/* An empty channels list is a special case for representing "all", an
	 * empty channels setting returns -EINVAL during _setup().
	 */
	if (!setup->channels[0]) {
		/* for "all" skip these, but you can still explicitly name them. */
		if ((module->flags & (TIL_MODULE_HERMETIC | TIL_MODULE_EXPERIMENTAL)))
			return 1;

		return 0;
	}

	for (char * const *channel = setup->channels; *channel; channel++) {
		if (!strcasecmp(module->name, *channel))
			return 0;
	}

	return 1;
}


static til_module_context_t * rtv_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	rtv_context_t		*ctxt;
	const til_module_t	**modules;
	size_t			n_modules, n_channels = 0;

	til_get_modules(&modules, &n_modules);

	/* how many modules are in the setup? */
	for (size_t i = 0; i < n_modules; i++) {
		if (!rtv_should_skip_module((rtv_setup_t *)setup, modules[i]))
			n_channels++;
	}

	ctxt = til_module_context_new(module, sizeof(rtv_context_t) + n_channels * sizeof(rtv_channel_t), stream, seed, ticks, n_cpus, path);
	if (!ctxt)
		return NULL;

	ctxt->duration = ((rtv_setup_t *)setup)->duration;
	ctxt->context_duration = ((rtv_setup_t *)setup)->context_duration;
	ctxt->snow_duration = ((rtv_setup_t *)setup)->snow_duration;
	ctxt->caption_duration = ((rtv_setup_t *)setup)->caption_duration;

	ctxt->snow_channel.module = &rtv_none_module;
	if (((rtv_setup_t *)setup)->snow_module) {
		ctxt->snow_channel.module = til_lookup_module(((rtv_setup_t *)setup)->snow_module);
		(void) til_module_create_context(ctxt->snow_channel.module, stream, rand_r(&seed), ticks, 0, path, NULL, &ctxt->snow_channel.module_ctxt);
	}

	ctxt->log_channels = ((rtv_setup_t *)setup)->log_channels;

	for (size_t i = 0; i < n_modules; i++) {
		if (!rtv_should_skip_module((rtv_setup_t *)setup, modules[i]))
			ctxt->channels[ctxt->n_channels++].module = modules[i];
	}

	setup_next_channel(ctxt, ticks);

	return &ctxt->til_module_context;
}


static void rtv_destroy_context(til_module_context_t *context)
{
	rtv_context_t	*ctxt = (rtv_context_t *)context;

	/* TODO FIXME: cleanup better, snow module etc */
	cleanup_channel(ctxt);
	free(context);
}


static void rtv_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	rtv_context_t	*ctxt = (rtv_context_t *)context;
	time_t		now = time(NULL);

	if (now >= ctxt->next_switch)
		setup_next_channel(ctxt, ticks);

	if (now >= ctxt->next_hide_caption)
		ctxt->caption = NULL;

	til_module_render(ctxt->channel->module_ctxt, stream, ticks, fragment_ptr);
}


static void rtv_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	rtv_context_t		*ctxt = (rtv_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	if (!ctxt->caption)
		return;

	txt_render_fragment(ctxt->caption, fragment, 0x00000000,
			    1, fragment->frame_height + 1,
			    (txt_align_t){
					.horiz = TXT_HALIGN_LEFT,
					.vert = TXT_VALIGN_BOTTOM
			    });
	txt_render_fragment(ctxt->caption, fragment, 0xffffffff,
			    0, fragment->frame_height,
			    (txt_align_t){
				.horiz = TXT_HALIGN_LEFT,
				.vert = TXT_VALIGN_BOTTOM
			    });
}


static int rtv_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*channels;
	const char	*duration;
	const char	*context_duration;
	const char	*caption_duration;
	const char	*snow_duration;
	const char	*snow_module;
	const char	*log_channels;
	const char	*log_channels_values[] = {
				"no",
				"yes",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Colon-separated list of channel modules, \"all\" for all",
							.key = "channels",
							.preferred = "compose",
							.annotations = NULL
						},
						&channels,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Channel duration, in seconds",
							.key = "duration",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(RTV_DURATION_SECS),
							.annotations = NULL
						},
						&duration,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Context duration, in seconds",
							.key = "context_duration",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(RTV_CONTEXT_DURATION_SECS),
							.annotations = NULL
						},
						&context_duration,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Caption duration, in seconds",
							.key = "caption_duration",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(RTV_CAPTION_DURATION_SECS),
							.annotations = NULL
						},
						&caption_duration,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Snow on channel-switch duration, in seconds",
							.key = "snow_duration",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(RTV_SNOW_DURATION_SECS),
							.annotations = NULL
						},
						&snow_duration,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Module for snow (\"blank\" for blanking, \"none\" to disable)",
							.key = "snow_module",
							.preferred = RTV_DEFAULT_SNOW_MODULE,
							.annotations = NULL
						},
						&snow_module,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Log channel settings to stderr",
							.key = "log_channels",
							.preferred = log_channels_values[RTV_DEFAULT_LOG_SETTINGS],
							.values = log_channels_values,
							.annotations = NULL
						},
						&log_channels,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		rtv_setup_t	*setup;

		/* FIXME: rtv_setup_t.snow_module needs freeing, so we need a bespoke free_func */
		setup = til_setup_new(sizeof(*setup) + sizeof(setup->channels[0]), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		/* turn channels colon-separated list into a null-terminated array of strings */
		if (strcasecmp(channels, "all")) {
			const til_module_t	**modules;
			size_t			n_modules;
			char			*tokchannels, *channel;
			int			n = 2;

			til_get_modules(&modules, &n_modules);

			tokchannels = strdup(channels);
			if (!tokchannels) {
				til_setup_free(&setup->til_setup);

				return -ENOMEM;
			}

			channel = strtok(tokchannels, ":");
			do {
				rtv_setup_t	*new;
				size_t		i;

				for (i = 0; i < n_modules; i++) {
					if (!strcasecmp(channel, modules[i]->name))
						break;
				}

				if (i >= n_modules) {
					til_setup_free(&setup->til_setup);

					return -EINVAL;
				}

				new = realloc(setup, sizeof(*setup) + n * sizeof(setup->channels[0]));
				if (!new) {
					til_setup_free(&setup->til_setup);

					return -ENOMEM;
				}

				new->channels[n - 2] = channel;
				new->channels[n - 1] = NULL;
				n++;

				setup = new;
			} while ((channel = strtok(NULL, ":")));
		}

		if (strcasecmp(snow_module, "none"))
			setup->snow_module = strdup(snow_module);

		/* TODO FIXME: parse errors */
		sscanf(duration, "%u", &setup->duration);
		sscanf(context_duration, "%u", &setup->context_duration);
		sscanf(caption_duration, "%u", &setup->caption_duration);
		sscanf(snow_duration, "%u", &setup->snow_duration);

		if (!strcasecmp(log_channels, log_channels_values[1]))
			setup->log_channels = 1;

		*res_setup = &setup->til_setup;
	}

	return 0;
}
