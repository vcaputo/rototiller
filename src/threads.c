#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

#include "fb.h"
#include "rototiller.h"
#include "threads.h"
#include "util.h"

typedef struct thread_t {
	threads_t	*threads;
	pthread_t	pthread;
	unsigned	id;
} thread_t;

typedef struct threads_t {
	unsigned		n_threads;

	pthread_mutex_t		idle_mutex;
	pthread_cond_t		idle_cond;
	unsigned		n_idle;

	pthread_mutex_t		frame_mutex;
	pthread_cond_t		frame_cond;
	void			(*render_fragment_func)(void *context, unsigned ticks, unsigned cpu, fb_fragment_t *fragment);
	void			*context;
	fb_fragment_t		*fragment;
	rototiller_fragmenter_t	fragmenter;
	unsigned		ticks;

	unsigned		next_fragment;
	unsigned		frame_num;

	thread_t		threads[];
} threads_t;


/* render fragments using the supplied render function */
static void * thread_func(void *_thread)
{
	thread_t	*thread = _thread;
	threads_t	*threads = thread->threads;
	unsigned	prev_frame_num = 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	for (;;) {

		/* wait for a new frame */
		pthread_mutex_lock(&threads->frame_mutex);
		pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &threads->frame_mutex);
		while (threads->frame_num == prev_frame_num)
			pthread_cond_wait(&threads->frame_cond, &threads->frame_mutex);
		prev_frame_num = threads->frame_num;
		pthread_cleanup_pop(1);

		/* render fragments */
		for (;;) {
			unsigned	frag_num;
			fb_fragment_t	fragment;

			frag_num = __sync_fetch_and_add(&threads->next_fragment, 1);

			if (!threads->fragmenter(threads->context, threads->fragment, frag_num, &fragment))
				break;

			threads->render_fragment_func(threads->context, threads->ticks, thread->id, &fragment);
		}

		/* report as idle */
		pthread_mutex_lock(&threads->idle_mutex);
		pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &threads->idle_mutex);
		threads->n_idle++;
		if (threads->n_idle == threads->n_threads)	/* Frame finished! Notify potential waiter. */
			pthread_cond_signal(&threads->idle_cond);
		pthread_cleanup_pop(1);
	}

	return NULL;
}


/* wait for all threads to be idle */
void threads_wait_idle(threads_t *threads)
{
	pthread_mutex_lock(&threads->idle_mutex);
	pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &threads->idle_mutex);
	while (threads->n_idle < threads->n_threads)
		pthread_cond_wait(&threads->idle_cond, &threads->idle_mutex);
	pthread_cleanup_pop(1);
}


/* submit a frame's fragments to the threads */
void threads_frame_submit(threads_t *threads, fb_fragment_t *fragment, rototiller_fragmenter_t fragmenter, void (*render_fragment_func)(void *context, unsigned ticks, unsigned cpu, fb_fragment_t *fragment), void *context, unsigned ticks)
{
	threads_wait_idle(threads);	/* XXX: likely non-blocking; already happens pre page flip */

	pthread_mutex_lock(&threads->frame_mutex);
	pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &threads->frame_mutex);
	threads->fragment = fragment;
	threads->fragmenter = fragmenter;
	threads->render_fragment_func = render_fragment_func;
	threads->context = context;
	threads->ticks = ticks;
	threads->frame_num++;
	threads->n_idle = threads->next_fragment = 0;
	pthread_cond_broadcast(&threads->frame_cond);
	pthread_cleanup_pop(1);
}


/* create threads instance, a thread per cpu is created */
threads_t * threads_create(void)
{
	unsigned	i, num = get_ncpus();
	threads_t	*threads;

	threads = calloc(1, sizeof(threads_t) + sizeof(thread_t) * num);
	if (!threads)
		return NULL;

	threads->n_idle = threads->n_threads = num;

	pthread_mutex_init(&threads->idle_mutex, NULL);
	pthread_cond_init(&threads->idle_cond, NULL);

	pthread_mutex_init(&threads->frame_mutex, NULL);
	pthread_cond_init(&threads->frame_cond, NULL);

	for (i = 0; i < num; i++) {
		thread_t	*thread = &threads->threads[i];

		thread->threads = threads;
		thread->id = i;
		pthread_create(&thread->pthread, NULL, thread_func, thread);
	}

	return threads;
}


/* destroy a threads instance */
void threads_destroy(threads_t *threads)
{
	unsigned	i;

	for (i = 0; i < threads->n_threads; i++)
		pthread_cancel(threads->threads[i].pthread);

	for (i = 0; i < threads->n_threads; i++)
		pthread_join(threads->threads[i].pthread, NULL);

	pthread_mutex_destroy(&threads->idle_mutex);
	pthread_cond_destroy(&threads->idle_cond);

	pthread_mutex_destroy(&threads->frame_mutex);
	pthread_cond_destroy(&threads->frame_cond);

	free(threads);
}


/* return the number of threads */
unsigned threads_num_threads(threads_t *threads)
{
	return threads->n_threads;
}
