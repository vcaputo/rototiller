#define SDL_MAIN_HANDLED
#include <assert.h>
#include <SDL.h>
#include <stdlib.h>
#include <errno.h>

#include "til_audio.h"
#include "til_audio_context.h"
#include "til_settings.h"
#include "til_setup.h"

/* sdl audio backend */

#define SDL_AUDIO_DEFAULT_SAMPLES	1024

typedef struct sdl_audio_setup_t {
	til_setup_t	til_setup;

	unsigned	frames;
} sdl_audio_setup_t;

typedef struct sdl_audio_t {
	til_audio_context_t	til_audio_context;

	SDL_AudioDeviceID	dev;
} sdl_audio_t;


static int sdl_audio_init(til_setup_t *setup, til_audio_context_t **res_context);
static void sdl_audio_shutdown(til_audio_context_t *context);
static int sdl_audio_queue(til_audio_context_t *context, int16_t *frames, int n_frames);
static unsigned sdl_audio_n_queued(til_audio_context_t *context);
static void sdl_audio_drop(til_audio_context_t *context);
static void sdl_audio_pause(til_audio_context_t *context);
static void sdl_audio_unpause(til_audio_context_t *context);
static int sdl_audio_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_audio_ops_t sdl_audio_ops = {
	.init = sdl_audio_init,
	.shutdown = sdl_audio_shutdown,
	.drop = sdl_audio_drop,
	.pause = sdl_audio_pause,
	.unpause = sdl_audio_unpause,
	.queue = sdl_audio_queue,
	.n_queued = sdl_audio_n_queued,
	.setup = sdl_audio_setup,
};


static int sdl_err_to_errno(int err)
{
	switch (err) {
	case SDL_ENOMEM:
		return ENOMEM;
	case SDL_EFREAD:
	case SDL_EFWRITE:
	case SDL_EFSEEK:
		return EIO;
	case SDL_UNSUPPORTED:
		return ENOTSUP;
	default:
		return EINVAL;
	}
}


static int sdl_audio_init(til_setup_t *setup, til_audio_context_t **res_context)
{
	sdl_audio_setup_t	*s = (sdl_audio_setup_t *)setup;
	sdl_audio_t		*c;
	int			r;

	assert(setup);
	assert(res_context);

	c = til_audio_context_new(&sdl_audio_ops, sizeof(sdl_audio_t), setup);
	if (!c)
		return -ENOMEM;

	SDL_SetMainReady(); /* is it a problem (or necessary) for both sdl_fb and sdl_audio to do this? */
	r = SDL_InitSubSystem(SDL_INIT_AUDIO);
	if (r < 0) {
		free(c);
		return -sdl_err_to_errno(r);
	}

	{
		SDL_AudioSpec	aspec = {
			.freq = 44100,
			.format = AUDIO_S16,
			.channels = 2,
			.samples = s->frames,
		};

		c->dev = SDL_OpenAudioDevice(NULL, 0, &aspec, NULL, 0);
		if (!c->dev) {
			/* SDL_GetError only returns strings, we need an errno FIXME */
			free(c);
			return -EPERM;
		}
	}

	*res_context = &c->til_audio_context;

	return 0;
}


static void sdl_audio_shutdown(til_audio_context_t *context)
{
	sdl_audio_t	*c = (sdl_audio_t *)context;

	SDL_CloseAudioDevice(c->dev);
}


static void sdl_audio_drop(til_audio_context_t *context)
{
	sdl_audio_t	*c = (sdl_audio_t *)context;

	SDL_ClearQueuedAudio(c->dev);
}


static void sdl_audio_pause(til_audio_context_t *context)
{
	sdl_audio_t	*c = (sdl_audio_t *)context;

	SDL_PauseAudioDevice(c->dev, 1);
}


static void sdl_audio_unpause(til_audio_context_t *context)
{
	sdl_audio_t	*c = (sdl_audio_t *)context;

	SDL_PauseAudioDevice(c->dev, 0);
}


static int sdl_audio_queue(til_audio_context_t *context, int16_t *frames, int n_frames)
{
	sdl_audio_t	*c = (sdl_audio_t *)context;

	/* TODO FIXME: SDL_audio.h says this returns a negative error code on
	 * error, but that's not the same as a -errno, so directly returning
	 * from here isn't exactly appropriate.
	 */
	return SDL_QueueAudio(c->dev, frames, n_frames * (sizeof(*frames) * 2));
}


static unsigned sdl_audio_n_queued(til_audio_context_t *context)
{
	sdl_audio_t	*c = (sdl_audio_t *)context;

	return SDL_GetQueuedAudioSize(c->dev) / (sizeof(int16_t) * 2);
}


static int sdl_audio_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*frames;
	const char	*frames_values[] = {
				"512",
				"1024",
				"2048",
				"4096",
				"8192",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Audio frames buffered",
							.key = "frames",
							.regex = "[0-9]+",
							.preferred = TIL_SETTINGS_STR(SDL_AUDIO_DEFAULT_SAMPLES),
							.values = frames_values,
							.annotations = NULL
						},
						&frames,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		sdl_audio_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &sdl_audio_ops);
		if (!setup)
			return -ENOMEM;

		if (sscanf(frames->value, "%u", &setup->frames) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, frames, res_setting, -EINVAL);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
