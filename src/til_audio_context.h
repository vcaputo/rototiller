#ifndef _TIL_AUDIO_CONTEXT_H
#define _TIL_AUDIO_CONTEXT_H

/* XXX: this isn't intended to be used outside of til_audio.[ch] */

#include <stddef.h>

typedef struct til_setup_t til_setup_t;
typedef struct til_audio_ops_t til_audio_ops_t;

typedef struct til_audio_context_t {
	til_setup_t		*setup;
	const til_audio_ops_t	*ops;
	const til_audio_hooks_t	*hooks;
	void			*hooks_context;
} til_audio_context_t;

void * til_audio_context_new(const til_audio_ops_t *ops, size_t size, til_setup_t *setup);
til_audio_context_t * til_audio_context_free(til_audio_context_t *audio_context);

#endif
