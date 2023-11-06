#include <stdint.h>
#include <stdio.h>

#include <playit.h>

#include "til.h"
#include "til_audio.h"
#include "til_fb.h"
#include "til_stream.h"

/* Copyright (C) 2023 - Vito Caputo <vcaputo@pengaru.com> */

/* .IT file playback module via libplayit (SchismTracker) */

#define PLAYIT_DEFAULT_ITFILE	"play.it"
#define PLAYIT_DEFAULT_SEEKABLE	0
#define PLAYIT_DEFAULT_BUFSIZE	4096


typedef struct playit_context_t {
	til_module_context_t	til_module_context;
	unsigned		last_frame;

	playit_t		*playit;
	til_audio_context_t	*audio;
	unsigned		paused:1;
	int16_t			buf[];
} playit_context_t;

typedef struct playit_setup_t {
	til_setup_t	til_setup;

	unsigned	seekable;
	unsigned	bufsize;
	char		itfile[];
} playit_setup_t;


static void playit_audio_seeked(void *hook_context, const til_audio_context_t *audio_context, unsigned ticks);
static void playit_audio_paused(void *hook_context, const til_audio_context_t *audio_context);
static void playit_audio_unpaused(void *hook_context, const til_audio_context_t *audio_context);


til_audio_hooks_t	playit_audio_hooks = {
	playit_audio_seeked,
	playit_audio_paused,
	playit_audio_unpaused,
};


static void playit_audio_seeked(void *hook_context, const til_audio_context_t *audio_context, unsigned ticks)
{
	playit_context_t	*ctxt = hook_context;

	assert(((playit_setup_t *)ctxt->til_module_context.setup)->seekable);

	playit_seek(ctxt->playit, ticks * 44.1f);
}


static void playit_audio_paused(void *hook_context, const til_audio_context_t *audio_context)
{
	playit_context_t	*ctxt = hook_context;

	ctxt->paused = 1;
}


static void playit_audio_unpaused(void *hook_context, const til_audio_context_t *audio_context)
{
	playit_context_t	*ctxt = hook_context;

	ctxt->paused = 0;
}


static til_module_context_t * playit_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	playit_setup_t		*s = (playit_setup_t *)setup;
	playit_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(playit_context_t) + s->bufsize * sizeof(ctxt->buf[0]) * 2, stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->audio = til_stream_get_audio_context(stream);
	if (!ctxt->audio)
		return til_module_context_free(&ctxt->til_module_context);

	if (til_audio_set_hooks(ctxt->audio, &playit_audio_hooks, ctxt) < 0)
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->playit = playit_open_file(s->itfile, s->seekable ? PLAYIT_FLAG_SEEKABLE : 0);
	if (!ctxt->playit)
		return til_module_context_free(&ctxt->til_module_context);

	return &ctxt->til_module_context;
}


static void playit_destroy_context(til_module_context_t *context)
{
	playit_context_t	*ctxt = (playit_context_t *)context;

	til_audio_unset_hooks(ctxt->audio, &playit_audio_hooks, ctxt);

	if (ctxt->playit)
		playit_destroy(ctxt->playit);

	free(ctxt);
}


static void playit_render_audio(til_module_context_t *context, til_stream_t *stream, unsigned ticks)
{
	playit_context_t	*ctxt = (playit_context_t *)context;
	playit_setup_t		*s = (playit_setup_t *)context->setup;
	size_t			tomix = s->bufsize;
	unsigned		frame, frames;

	if (ctxt->paused)
		return;

	tomix -= til_audio_n_queued(ctxt->audio);
	if (tomix <= 0)
		return;

	frames = playit_update(ctxt->playit, ctxt->buf, tomix * sizeof(int16_t) * 2, &frame);
	if (!frames)
		return;

	til_audio_queue(ctxt->audio, ctxt->buf, frames);
}


static int playit_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	playit_module = {
	.create_context = playit_create_context,
	.destroy_context = playit_destroy_context,
	.render_audio = playit_render_audio,
	.setup = playit_setup,
	.name = "playit",
	.description = ".IT tracked music file player",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_EXPERIMENTAL,
};


static int playit_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*itfile;
	til_setting_t	*seekable;
	til_setting_t	*bufsize;
	const char	*seekable_values[] = {
				"off",
				"on",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = ".IT file path",
							.key = "itfile",
							.preferred = PLAYIT_DEFAULT_ITFILE,
							.annotations = NULL
						},
						&itfile,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Seekable",
							.key = "seekable",
							.regex = "^(on|off)",
							.preferred = seekable_values[PLAYIT_DEFAULT_SEEKABLE],
							.values = seekable_values,
							.annotations = NULL
						},
						&seekable,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Buffer size in frames",
							.key = "bufsize",
							.preferred = TIL_SETTINGS_STR(PLAYIT_DEFAULT_BUFSIZE),
						},
						&bufsize,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		playit_setup_t	*setup;
		size_t		itfile_len = strlen(itfile->value);

		setup = til_setup_new(settings, sizeof(*setup) + itfile_len + 1, NULL, &playit_module);
		if (!setup)
			return -ENOMEM;

		strncpy(setup->itfile, itfile->value, itfile_len);

		r = til_value_to_pos(seekable_values, seekable->value, &setup->seekable);
		if (r < 0)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, seekable, res_setting, r);

		if (sscanf(bufsize->value, "%u", &setup->bufsize) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, bufsize, res_setting, -EINVAL);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
