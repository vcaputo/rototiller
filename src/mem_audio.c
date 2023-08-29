#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "til.h"
#include "til_audio.h"
#include "til_audio_context.h"
#include "til_settings.h"
#include "til_setup.h"

/* "mem" audio backend */

typedef struct mem_audio_setup_t {
	til_setup_t	til_setup;
} mem_audio_setup_t;

typedef struct mem_audio_t {
	til_audio_context_t	til_audio_context;

	unsigned		n_queued;
	unsigned		n_queued_start_ticks;
	unsigned		paused:1;
} mem_audio_t;


static int mem_audio_init(til_setup_t *setup, til_audio_context_t **res_context);
static int mem_audio_queue(til_audio_context_t *context, int16_t *frames, int n_frames);;
static unsigned mem_audio_n_queued(til_audio_context_t *context);
static void mem_audio_drop(til_audio_context_t *context);
static void mem_audio_pause(til_audio_context_t *context);
static void mem_audio_unpause(til_audio_context_t *context);
static int mem_audio_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_audio_ops_t mem_audio_ops = {
	.init = mem_audio_init,
	.drop = mem_audio_drop,
	.pause = mem_audio_pause,
	.unpause = mem_audio_unpause,
	.queue = mem_audio_queue,
	.n_queued = mem_audio_n_queued,
	.setup = mem_audio_setup,
};


/* this simulates an audio timer grinding through the queued frames when unpaused,
 * returns the remaining n_queued (if any) for convenience, but it's also maintained
 * at c->n_queued.
 */
static unsigned mem_refresh_n_queued(mem_audio_t *c)
{
	if (!c->paused && c->n_queued) {
		unsigned	now = til_ticks_now();
		unsigned	n_played = ((float)(now - c->n_queued_start_ticks) * 44.1f);

		c->n_queued -= MIN(c->n_queued, n_played);
	}

	return c->n_queued;
}


static int mem_audio_init(til_setup_t *setup, til_audio_context_t **res_context)
{
	mem_audio_t	*c;

	assert(setup);
	assert(res_context);

	c = til_audio_context_new(&mem_audio_ops, sizeof(mem_audio_t), setup);
	if (!c)
		return -ENOMEM;

	c->paused = 1;
	*res_context = &c->til_audio_context;

	return 0;
}


static void mem_audio_drop(til_audio_context_t *context)
{
	mem_audio_t	*c = (mem_audio_t *)context;

	c->n_queued = 0;
}


static void mem_audio_pause(til_audio_context_t *context)
{
	mem_audio_t	*c = (mem_audio_t *)context;

	if (!c->paused) {
		mem_refresh_n_queued(c);
		c->paused = 1;
	}
}


static void mem_audio_unpause(til_audio_context_t *context)
{
	mem_audio_t	*c = (mem_audio_t *)context;

	if (c->paused) {
		c->paused = 0;
		c->n_queued_start_ticks = til_ticks_now();
	}
}


static int mem_audio_queue(til_audio_context_t *context, int16_t *frames, int n_frames)
{
	mem_audio_t	*c = (mem_audio_t *)context;

	mem_refresh_n_queued(c);
	c->n_queued += n_frames;

	return 0;
}


static unsigned mem_audio_n_queued(til_audio_context_t *context)
{
	mem_audio_t	*c = (mem_audio_t *)context;

	return mem_refresh_n_queued(c);
}


static int mem_audio_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	if (res_setup) {
		mem_audio_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &mem_audio_ops);
		if (!setup)
			return -ENOMEM;

		*res_setup = &setup->til_setup;
	}

	return 0;
}
