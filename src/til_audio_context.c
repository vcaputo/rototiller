#include <stdlib.h>

#include "til_audio.h"
#include "til_audio_context.h"
#include "til_setup.h"

/* XXX: this isn't intended to be used by anything other than til_audio.[ch],
 * use til_audio_{init,shutdown}().  This is purely in service of that.
 */

void * til_audio_context_new(const til_audio_ops_t *ops, size_t size, til_setup_t *setup)
{
	til_audio_context_t	*c;

	c = calloc(1, size);
	if (!c)
		return NULL;

	c->setup = til_setup_ref(setup);
	c->ops = ops;

	return c;
}


til_audio_context_t * til_audio_context_free(til_audio_context_t *audio_context)
{
	if (!audio_context)
		return NULL;

	til_setup_free(audio_context->setup);
	free(audio_context);

	return NULL;
}
