#include <stdlib.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"
#include "til_settings.h"
#include "til_util.h"

#include "txt/txt.h"

/* Copyright (C) 2019 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements an MTV-inspired random slideshow of rototiller modules.
 *
 * Some TODO items:
 * - optionally persist module contexts so they resume rather than restart
 */

#define RTV_SNOW_DURATION_SECS		1
#define RTV_DURATION_SECS		15
#define RTV_CAPTION_DURATION_SECS	5
#define RTV_CONTEXT_DURATION_SECS	60

typedef struct rtv_channel_t {
	const til_module_t	*module;
	void			*module_ctxt;
	time_t			last_on_time, cumulative_time;
	char			*settings;
	txt_t			*caption;
	unsigned		order;
} rtv_channel_t;

typedef struct rtv_context_t {
	unsigned		n_cpus;

	time_t			next_switch, next_hide_caption;
	rtv_channel_t		*channel, *last_channel;
	txt_t			*caption;

	rtv_channel_t		snow_channel;

	size_t			n_channels;
	rtv_channel_t		channels[];
} rtv_context_t;

static void setup_next_channel(rtv_context_t *ctxt, unsigned ticks);
static void * rtv_create_context(unsigned ticks, unsigned num_cpus);
static void rtv_destroy_context(void *context);
static void rtv_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter);
static void rtv_finish_frame(void *context, unsigned ticks, til_fb_fragment_t *fragment);
static int rtv_setup(const til_settings_t *settings, const til_setting_t **res_setting, const til_setting_desc_t **res_desc);

static unsigned rtv_duration = RTV_DURATION_SECS;
static unsigned rtv_context_duration = RTV_CONTEXT_DURATION_SECS;
static unsigned rtv_snow_duration = RTV_SNOW_DURATION_SECS;
static unsigned rtv_caption_duration = RTV_CAPTION_DURATION_SECS;
static char	**rtv_channels;
static char 	*rtv_snow_module;


til_module_t	rtv_module = {
	.create_context = rtv_create_context,
	.destroy_context = rtv_destroy_context,
	.prepare_frame = rtv_prepare_frame,
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
		ctxt->channels[i].order = rand();

	qsort(ctxt->channels, ctxt->n_channels, sizeof(rtv_channel_t), cmp_channels);
}


static char * randomize_module_setup(const til_module_t *module)
{
	til_settings_t			*settings;
	const til_setting_t		*setting;
	const til_setting_desc_t	*desc;
	char				*arg;

	if (!module->setup)
		return NULL;

	settings = til_settings_new(NULL);
	if (!settings)
		return NULL;

	while (module->setup(settings, &setting, &desc) > 0) {
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

	arg = til_settings_as_arg(settings);
	til_settings_free(settings);

	return arg;
}


static void setup_next_channel(rtv_context_t *ctxt, unsigned ticks)
{
	time_t	now = time(NULL);

	/* TODO: most of this module stuff should probably be
	 * in rototiller.c helpers, but it's harmless for now.
	 */
	if (ctxt->channel) {
		ctxt->channel->cumulative_time += now - ctxt->channel->last_on_time;
		if (ctxt->channel->cumulative_time >= rtv_context_duration) {
			ctxt->channel->cumulative_time = 0;

			if (ctxt->channel->module->destroy_context)
				ctxt->channel->module->destroy_context(ctxt->channel->module_ctxt);
			ctxt->channel->module_ctxt = NULL;

			free(ctxt->channel->settings);
			ctxt->channel->settings = NULL;

			ctxt->caption = ctxt->channel->caption = txt_free(ctxt->channel->caption);
		}
	}

	if (!ctxt->n_channels || ctxt->channel != &ctxt->snow_channel) {
		ctxt->last_channel = ctxt->channel;
		ctxt->channel = &ctxt->snow_channel;
		ctxt->caption = NULL;
		ctxt->next_switch = now + rtv_snow_duration;
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

		if (!ctxt->channel->settings) {
			char	*settings;
			txt_t	*caption;

			settings = randomize_module_setup(ctxt->channel->module);
			caption = txt_newf("Title: %s%s%s\nDescription: %s%s%s%s%s",
						 ctxt->channel->module->name,
						 ctxt->channel->module->author ? "\nAuthor: " : "",
						 ctxt->channel->module->author ? : "",
						 ctxt->channel->module->description,
						 ctxt->channel->module->license ? "\nLicense: " : "",
						 ctxt->channel->module->license ? : "",
						 settings ? "\nSettings: " : "",
						 settings ? settings : "");

			ctxt->caption = ctxt->channel->caption = caption;
			ctxt->channel->settings = settings ? settings : strdup("");
		}

		ctxt->next_switch = now + rtv_duration;
		ctxt->next_hide_caption = now + rtv_caption_duration;
	}

	if (!ctxt->channel->module_ctxt && ctxt->channel->module->create_context)
		ctxt->channel->module_ctxt = ctxt->channel->module->create_context(ticks, ctxt->n_cpus);

	ctxt->channel->last_on_time = now;
}


static int rtv_should_skip_module(const rtv_context_t *ctxt, const til_module_t *module)
{
	if (module == &rtv_module ||
	    module == ctxt->snow_channel.module)
		return 1;

	if (!rtv_channels)
		return 0;

	for (char **channel = rtv_channels; *channel; channel++) {
		if (!strcmp(module->name, *channel))
			return 0;
	}

	return 1;
}


static void * rtv_create_context(unsigned ticks, unsigned num_cpus)
{
	rtv_context_t		*ctxt;
	const til_module_t	**modules;
	size_t			n_modules;
	static til_module_t	none_module = {};

	til_get_modules(&modules, &n_modules);

	ctxt = calloc(1, sizeof(rtv_context_t) + n_modules * sizeof(rtv_channel_t));
	if (!ctxt)
		return NULL;

	ctxt->n_cpus = num_cpus;

	ctxt->snow_channel.module = &none_module;
	if (rtv_snow_module) {
		ctxt->snow_channel.module = til_lookup_module(rtv_snow_module);
		if (ctxt->snow_channel.module->create_context)
			ctxt->snow_channel.module_ctxt = ctxt->snow_channel.module->create_context(ticks, ctxt->n_cpus);
	}

	for (size_t i = 0; i < n_modules; i++) {
		if (rtv_should_skip_module(ctxt, modules[i]))
			continue;

		ctxt->channels[ctxt->n_channels++].module = modules[i];
	}

	setup_next_channel(ctxt, ticks);

	return ctxt;
}


static void rtv_destroy_context(void *context)
{
	free(context);
}


static void rtv_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter)
{
	rtv_context_t	*ctxt = context;
	time_t		now = time(NULL);

	if (now >= ctxt->next_switch)
		setup_next_channel(ctxt, ticks);

	if (now >= ctxt->next_hide_caption)
		ctxt->caption = NULL;

	/* there's a special-case "none" (or unconfigured) snow module, that just blanks,
	 * it's a nil module so just implement it here.
	 */
	if (!ctxt->channel->module->render_fragment &&
	    !ctxt->channel->module->prepare_frame)
		til_fb_fragment_zero(fragment);
	else
		til_module_render(ctxt->channel->module, ctxt->channel->module_ctxt, ticks, fragment);
}


static void rtv_finish_frame(void *context, unsigned ticks, til_fb_fragment_t *fragment)
{
	rtv_context_t	*ctxt = context;

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


static int rtv_setup(const til_settings_t *settings, const til_setting_t **res_setting, const til_setting_desc_t **res_desc)
{
	const char	*channels;
	const char	*duration;
	const char	*context_duration;
	const char	*caption_duration;
	const char	*snow_duration;
	const char	*snow_module;
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Colon-Separated List Of Channel Modules",
							.key = "channels",
							.preferred = "all",
							.annotations = NULL
						},
						&channels,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Channel Duration In Seconds",
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
						&(til_setting_desc_t){
							.name = "Context Duration In Seconds",
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
						&(til_setting_desc_t){
							.name = "Caption Duration In Seconds",
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
						&(til_setting_desc_t){
							.name = "Snow On Channel Switch Duration In Seconds",
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
						&(til_setting_desc_t){
							.name = "Module To Use For Snow (\"none\" To Blank)",
							.key = "snow_module",
							.preferred = "snow",
							.annotations = NULL
						},
						&snow_module,
						res_setting,
						res_desc);
	if (r)
		return r;

	/* turn channels colon-separated list into a null-terminated array of strings */
	if (strcmp(channels, "all")) {
		const til_module_t	**modules;
		size_t			n_modules;
		char			*tokchannels, *channel;
		int			n = 2;

		til_get_modules(&modules, &n_modules);

		tokchannels = strdup(channels);
		if (!tokchannels)
			return -ENOMEM;

		channel = strtok(tokchannels, ":");
		do {
			char	**new;
			size_t	i;

			for (i = 0; i < n_modules; i++) {
				if (!strcmp(channel, modules[i]->name))
					break;
			}

			if (i >= n_modules)
				return -EINVAL;

			new = realloc(rtv_channels, n * sizeof(*rtv_channels));
			if (!new)
				return -ENOMEM;

			new[n - 2] = channel;
			new[n - 1] = NULL;
			n++;

			rtv_channels = new;
		} while (channel = strtok(NULL, ":"));
	}

	if (strcmp(snow_module, "none"))
		rtv_snow_module = strdup(snow_module);

	/* TODO FIXME: parse errors */
	sscanf(duration, "%u", &rtv_duration);
	sscanf(context_duration, "%u", &rtv_context_duration);
	sscanf(caption_duration, "%u", &rtv_caption_duration);
	sscanf(snow_duration, "%u", &rtv_snow_duration);

	return 0;
}
