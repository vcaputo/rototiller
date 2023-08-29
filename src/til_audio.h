#ifndef _TIL_AUDIO_H
#define _TIL_AUDIO_H

#include <stdint.h>

typedef struct til_settings_t til_settings_t;
typedef struct til_setting_t til_setting_t;
typedef struct til_setting_desc_t til_setting_desc_t;
typedef struct til_setup_t til_setup_t;
typedef struct til_audio_context_t til_audio_context_t;

typedef struct til_audio_ops_t {
	int		(*setup)(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);
	int		(*init)(til_setup_t *setup, til_audio_context_t **res_audio_context);
	void		(*shutdown)(til_audio_context_t *audio_context);
	void		(*drop)(til_audio_context_t *audio_context);
	void		(*pause)(til_audio_context_t *audio_context);
	void		(*unpause)(til_audio_context_t *audio_context);
	int		(*queue)(til_audio_context_t *audio_context, int16_t *frames, int n_frames);
	unsigned	(*n_queued)(til_audio_context_t *audio_context);
} til_audio_ops_t;

typedef struct til_audio_hooks_t {
	void	(*seeked)(void *hooks_context, const til_audio_context_t *audio_context, unsigned ticks);
	void	(*paused)(void *hooks_context, const til_audio_context_t *audio_context);
	void	(*unpaused)(void *hooks_context, const til_audio_context_t *audio_context);
} til_audio_hooks_t;

int til_audio_open(const til_audio_ops_t *ops, til_setup_t *setup, til_audio_context_t **res_audio_context);
void til_audio_shutdown(til_audio_context_t *audio_context);
int til_audio_set_hooks(til_audio_context_t *audio_context, til_audio_hooks_t *hooks, void *hooks_context);
int til_audio_unset_hooks(til_audio_context_t *audio_context, til_audio_hooks_t *hooks, void *hooks_context);
int til_audio_queue(til_audio_context_t *audio_context, int16_t *frames, int n_frames);
unsigned til_audio_n_queued(til_audio_context_t *audio_context);
void til_audio_seek(til_audio_context_t *audio_context, unsigned ticks);
void til_audio_pause(til_audio_context_t *audio_context);
void til_audio_unpause(til_audio_context_t *audio_context);

#endif
