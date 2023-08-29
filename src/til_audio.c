#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "til_audio.h"
#include "til_audio_context.h"


/* initialize audio via ops using setup, context stored in res_audio_context on success.
 * returns -errno on error, 0 on success
 * playback is left in a paused state, with an empty queue
 */
int til_audio_open(const til_audio_ops_t *ops, til_setup_t *setup, til_audio_context_t **res_audio_context)
{
	til_audio_context_t	*c;
	int			r;

	assert(ops);
	assert(ops->init);
	assert(ops->queue);
	assert(ops->n_queued);
	assert(setup);
	assert(res_audio_context);

	r = ops->init(setup, &c);
	if (r < 0)
		return r;

	*res_audio_context = c;

	return 0;
}


/* closes audio and frees context,
 * callers are expected to use this and not til_audio_context_free().
 */
void til_audio_shutdown(til_audio_context_t *audio_context)
{
	assert(audio_context);
	assert(audio_context->ops);

	if (audio_context->ops->shutdown)
		audio_context->ops->shutdown(audio_context);

	til_audio_context_free(audio_context);
}


/* install audio hooks to receive notification on events like seek/pause/unpause */
int til_audio_set_hooks(til_audio_context_t *audio_context, til_audio_hooks_t *hooks, void *hooks_context)
{
	assert(audio_context);
	assert(hooks);

	if (audio_context->hooks && audio_context->hooks != hooks)
		return -EEXIST;

	audio_context->hooks = hooks;
	audio_context->hooks_context = hooks_context;

	return 0;
}


/* remove audio hooks */
int til_audio_unset_hooks(til_audio_context_t *audio_context, til_audio_hooks_t *hooks, void *hooks_context)
{
	assert(audio_context);
	assert(hooks);

	/* this is kind of silly, but seems potentially useful in the defensive department */
	if (audio_context->hooks != hooks ||
	    audio_context->hooks_context != hooks_context)
		return -EINVAL;

	audio_context->hooks = NULL;
	audio_context->hooks_context = NULL;

	return 0;
}


/* seek to an absolute ticks, playback is left in a paused state with an empty queue */
void til_audio_seek(til_audio_context_t *audio_context, unsigned ticks)
{
	assert(audio_context);

	if (audio_context->ops->pause)
		audio_context->ops->pause(audio_context);

	if (audio_context->ops->drop)
		audio_context->ops->drop(audio_context);

	if (audio_context->hooks && audio_context->hooks->seeked)
		audio_context->hooks->seeked(audio_context->hooks_context, audio_context, ticks);
}


/* queue n_frames frames to audio_context,
 * returns 0 on success, -errno on error
 */
int til_audio_queue(til_audio_context_t *audio_context, int16_t *frames, int n_frames)
{
	assert(audio_context);
	assert(frames);
	assert(n_frames > 0);

	return audio_context->ops->queue(audio_context, frames, n_frames);
}


/* query how many frames are currently queued
 * returns 0 on success, -errno on error
 */
unsigned til_audio_n_queued(til_audio_context_t *audio_context)
{
	assert(audio_context);

	return audio_context->ops->n_queued(audio_context);
}


/* pause the underlying audio playback, queue is left as-is, should be idempotent */
void til_audio_pause(til_audio_context_t *audio_context)
{
	assert(audio_context);

	if (audio_context->ops->pause)
		audio_context->ops->pause(audio_context);

	if (audio_context->hooks && audio_context->hooks->paused)
		audio_context->hooks->paused(audio_context->hooks_context, audio_context);
}


/* unpause the underlying audio playback, queue is left as-is, should be idempotent */
void til_audio_unpause(til_audio_context_t *audio_context)
{
	assert(audio_context);

	if (audio_context->ops->unpause)
		audio_context->ops->unpause(audio_context);

	if (audio_context->hooks && audio_context->hooks->unpaused)
		audio_context->hooks->unpaused(audio_context->hooks_context, audio_context);
}
