#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

#include "fb.h"
#include "rototiller.h"
#include "threads.h"
#include "util.h"

/* This is a very simple/naive implementation, there's certainly room for improvement.
 * Some things to explore:
 *  - switch to a single condition variable and broadcast to wake up the threads?
 *  - use lock-free algorithms?
 */

typedef struct fragment_node_t fragment_node_t;

struct fragment_node_t {
	fragment_node_t	*next;
	fb_fragment_t	*fragment;
};

typedef struct thread_t {
	pthread_t	thread;
	pthread_mutex_t	mutex;
	pthread_cond_t	cond;
	void		(*render_fragment_func)(void *context, fb_fragment_t *fragment);
	void		*context;
	fragment_node_t	*fragments;
} thread_t;

typedef struct threads_t {
	unsigned	n_threads;
	fragment_node_t	fragment_nodes[ROTOTILLER_FRAME_MAX_FRAGMENTS];
	thread_t	threads[];
} threads_t;


/* render submitted fragments using the supplied render function */
static void * thread_func(void *_thread)
{
	thread_t	*thread = _thread;

	for (;;) {
		pthread_mutex_lock(&thread->mutex);
		while (!thread->fragments)
			pthread_cond_wait(&thread->cond, &thread->mutex);

		do {
			thread->render_fragment_func(thread->context, thread->fragments->fragment);
			thread->fragments = thread->fragments->next;
		} while (thread->fragments);

		pthread_mutex_unlock(&thread->mutex);
		pthread_cond_signal(&thread->cond);
	}

	return NULL;
}


/* submit a list of fragments to render using the specified thread and render_fragment_func */
static void thread_fragments_submit(thread_t *thread, void (*render_fragment_func)(void *context, fb_fragment_t *fragment), void *context, fragment_node_t *fragments)
{
	pthread_mutex_lock(&thread->mutex);
	while (thread->fragments != NULL)	/* XXX: never true due to thread_wait_idle() */
		pthread_cond_wait(&thread->cond, &thread->mutex);

	thread->render_fragment_func = render_fragment_func;
	thread->context = context;
	thread->fragments = fragments;

	pthread_mutex_unlock(&thread->mutex);
	pthread_cond_signal(&thread->cond);
}


/* wait for a thread to be idle */
static void thread_wait_idle(thread_t *thread)
{
	pthread_mutex_lock(&thread->mutex);
	while (thread->fragments)
		pthread_cond_wait(&thread->cond, &thread->mutex);
	pthread_mutex_unlock(&thread->mutex);
}


/* submit a frame's fragments to the threads */
void threads_frame_submit(threads_t *threads, rototiller_frame_t *frame, void (*render_fragment_func)(void *context, fb_fragment_t *fragment), void *context)
{
	unsigned	i, t;
	fragment_node_t	*lists[threads->n_threads];

	assert(frame->n_fragments <= ROTOTILLER_FRAME_MAX_FRAGMENTS);

	for (i = 0; i < threads->n_threads; i++)
		lists[i] = NULL;

	for (i = 0; i < frame->n_fragments;) {
		for (t = 0; i < frame->n_fragments && t < threads->n_threads; t++, i++) {
			threads->fragment_nodes[i].next = lists[t];
			lists[t] = &threads->fragment_nodes[i];
			lists[t]->fragment = &frame->fragments[i];
		}
	}

	for (i = 0; i < threads->n_threads; i++)
		thread_fragments_submit(&threads->threads[i], render_fragment_func, context, lists[i]);
}


/* wait for all threads to drain their fragments list and become idle */
void threads_wait_idle(threads_t *threads)
{
	unsigned	i;

	for (i = 0; i < threads->n_threads; i++)
		thread_wait_idle(&threads->threads[i]);
}


/* create threads instance, a thread per cpu is created */
threads_t * threads_create(void)
{
	threads_t	*threads;
	unsigned	i, num = get_ncpus();

	threads = calloc(1, sizeof(threads_t) + sizeof(thread_t) * num);
	if (!threads)
		return NULL;

	for (i = 0; i < num; i++) {
		pthread_mutex_init(&threads->threads[i].mutex, NULL);
		pthread_cond_init(&threads->threads[i].cond, NULL);
		pthread_create(&threads->threads[i].thread, NULL, thread_func, &threads->threads[i]);
	}

	threads->n_threads = num;

	return threads;
}


/* destroy a threads instance */
void threads_destroy(threads_t *threads)
{
	unsigned	i;

	for (i = 0; i < threads->n_threads; i++)
		pthread_cancel(threads->threads[i].thread);

	for (i = 0; i < threads->n_threads; i++)
		pthread_join(threads->threads[i].thread, NULL);

	free(threads);
}


/* return the number of threads */
unsigned threads_num_threads(threads_t *threads)
{
	return threads->n_threads;
}
