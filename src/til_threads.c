#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

#include "til.h"
#include "til_fb.h"
#include "til_threads.h"
#include "til_util.h"

typedef struct til_thread_t {
	til_threads_t	*threads;
	pthread_t	pthread;
	unsigned	id;
} til_thread_t;

typedef struct til_threads_t {
	unsigned		n_threads;

	pthread_mutex_t		idle_mutex;
	pthread_cond_t		idle_cond;
	unsigned		n_idle;

	pthread_mutex_t		frame_mutex;
	pthread_cond_t		frame_cond;
	void			(*render_fragment_func)(til_module_context_t *context, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr);
	void			*context;
	til_fb_fragment_t	**fragment_ptr;
	til_frame_plan_t	frame_plan;
	unsigned		ticks;

	unsigned		next_fragment;
	unsigned		frame_num;

	til_thread_t		threads[];
} til_threads_t;


/* render fragments using the supplied render function */
static void * thread_func(void *_thread)
{
	til_thread_t	*thread = _thread;
	til_threads_t	*threads = thread->threads;
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

		if (threads->frame_plan.cpu_affinity) { /* render only fragments for my thread->id */
			unsigned frag_num = thread->id;

			/* This is less performant, since we'll spin until our fragnum comes up,
			 * rather than just rendering whatever's next whenever we're available.
			 *
			 * Some modules allocate persistent per-cpu state affecting the contents of fragments,
			 * which may require a consistent mapping of CPU to fragnum across frames.
			 */
			for (;;) {
				til_fb_fragment_t	frag, *frag_ptr = &frag;

				while (!__sync_bool_compare_and_swap(&threads->next_fragment, frag_num, frag_num + 1));

				if (!threads->frame_plan.fragmenter(threads->context, *(threads->fragment_ptr), frag_num, &frag))
					break;

				threads->render_fragment_func(threads->context, threads->ticks, thread->id, &frag_ptr);
				frag_num += threads->n_threads;
			}
		} else { /* render *any* available fragment */
			for (;;) {
				unsigned		frag_num;
				til_fb_fragment_t	frag, *frag_ptr = &frag;

				frag_num = __sync_fetch_and_add(&threads->next_fragment, 1);

				if (!threads->frame_plan.fragmenter(threads->context, *(threads->fragment_ptr), frag_num, &frag))
					break;

				threads->render_fragment_func(threads->context, threads->ticks, thread->id, &frag_ptr);
			}
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
void til_threads_wait_idle(til_threads_t *threads)
{
	pthread_mutex_lock(&threads->idle_mutex);
	pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &threads->idle_mutex);
	while (threads->n_idle < threads->n_threads)
		pthread_cond_wait(&threads->idle_cond, &threads->idle_mutex);
	pthread_cleanup_pop(1);
}


/* submit a frame's fragments to the threads */
void til_threads_frame_submit(til_threads_t *threads, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *frame_plan, void (*render_fragment_func)(til_module_context_t *context, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr), til_module_context_t *context, unsigned ticks)
{
	til_threads_wait_idle(threads);	/* XXX: likely non-blocking; already happens pre page flip */

	pthread_mutex_lock(&threads->frame_mutex);
	pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &threads->frame_mutex);
	threads->fragment_ptr = fragment_ptr;
	threads->frame_plan = *frame_plan;
	threads->render_fragment_func = render_fragment_func;
	threads->context = context;
	threads->ticks = ticks;
	threads->frame_num++;
	threads->n_idle = threads->next_fragment = 0;
	pthread_cond_broadcast(&threads->frame_cond);
	pthread_cleanup_pop(1);
}


/* create threads instance, a thread per cpu is created */
til_threads_t * til_threads_create(void)
{
	unsigned	num = til_get_ncpus();
	til_threads_t	*threads;

	threads = calloc(1, sizeof(til_threads_t) + sizeof(til_thread_t) * num);
	if (!threads)
		return NULL;

	threads->n_idle = threads->n_threads = num;

	pthread_mutex_init(&threads->idle_mutex, NULL);
	pthread_cond_init(&threads->idle_cond, NULL);

	pthread_mutex_init(&threads->frame_mutex, NULL);
	pthread_cond_init(&threads->frame_cond, NULL);

	for (unsigned i = 0; i < num; i++) {
		til_thread_t	*thread = &threads->threads[i];

		thread->threads = threads;
		thread->id = i;
		pthread_create(&thread->pthread, NULL, thread_func, thread);
	}

	return threads;
}


/* destroy a threads instance */
void til_threads_destroy(til_threads_t *threads)
{
	for (unsigned i = 0; i < threads->n_threads; i++)
		pthread_cancel(threads->threads[i].pthread);

	for (unsigned i = 0; i < threads->n_threads; i++)
		pthread_join(threads->threads[i].pthread, NULL);

	pthread_mutex_destroy(&threads->idle_mutex);
	pthread_cond_destroy(&threads->idle_cond);

	pthread_mutex_destroy(&threads->frame_mutex);
	pthread_cond_destroy(&threads->frame_cond);

	free(threads);
}


/* return the number of threads */
unsigned til_threads_num_threads(til_threads_t *threads)
{
	return threads->n_threads;
}
