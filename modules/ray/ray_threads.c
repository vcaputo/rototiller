#include <pthread.h>
#include <stdlib.h>

#include "fb.h"

#include "ray_scene.h"
#include "ray_threads.h"

#define BUSY_WAIT_NUM	1000000000	/* How much to spin before sleeping in pthread_cond_wait() */

/* for now assuming x86 */
#define cpu_relax() \
	__asm__ __volatile__ ( "pause\n" : : : "memory")

/* This is a very simple/naive implementation, there's certainly room for improvement.
 *
 * Without the BUSY_WAIT_NUM spinning this approach seems to leave a fairly
 * substantial proportion of CPU idle while waiting for the render thread to
 * complete on my core 2 duo.
 *
 * It's probably just latency in getting the render thread woken when the work
 * is submitted, and since the fragments are split equally the main thread gets
 * a head start and has to wait when it finishes first.  The spinning is just
 * an attempt to avoid going to sleep while the render threads finish, there
 * still needs to be improvement in how the work is submitted.
 *
 * I haven't spent much time on optimizing the raytracer yet.
 */

static void * ray_thread_func(void *_thread)
{
	ray_thread_t	*thread = _thread;

	for (;;) {
		pthread_mutex_lock(&thread->mutex);
		while (thread->fragment == NULL)
			pthread_cond_wait(&thread->cond, &thread->mutex);

		ray_scene_render_fragment(thread->scene, thread->camera, thread->fragment);
		thread->fragment = NULL;
		pthread_mutex_unlock(&thread->mutex);
		pthread_cond_signal(&thread->cond);
	}

	return NULL;
}


void ray_thread_fragment_submit(ray_thread_t *thread, ray_scene_t *scene, ray_camera_t *camera, fb_fragment_t *fragment)
{
	pthread_mutex_lock(&thread->mutex);
	while (thread->fragment != NULL)	/* XXX: never true due to ray_thread_wait_idle() */
		pthread_cond_wait(&thread->cond, &thread->mutex);

	thread->fragment = fragment;
	thread->scene = scene;
	thread->camera = camera;

	pthread_mutex_unlock(&thread->mutex);
	pthread_cond_signal(&thread->cond);
}


void ray_thread_wait_idle(ray_thread_t *thread)
{
	unsigned	n;

	/* Spin before going to sleep, the other thread should not take substantially longer. */
	for (n = 0; thread->fragment != NULL && n < BUSY_WAIT_NUM; n++)
		cpu_relax();

	pthread_mutex_lock(&thread->mutex);
	while (thread->fragment != NULL)
		pthread_cond_wait(&thread->cond, &thread->mutex);
	pthread_mutex_unlock(&thread->mutex);
}


ray_threads_t * ray_threads_create(unsigned num)
{
	ray_threads_t	*threads;
	unsigned	i;

	threads = malloc(sizeof(ray_threads_t) + sizeof(ray_thread_t) * num);
	if (!threads)
		return NULL;

	for (i = 0; i < num; i++) {
		pthread_mutex_init(&threads->threads[i].mutex, NULL);
		pthread_cond_init(&threads->threads[i].cond, NULL);
		threads->threads[i].fragment = NULL;
		pthread_create(&threads->threads[i].thread, NULL, ray_thread_func, &threads->threads[i]);
	}
	threads->n_threads = num;

	return threads;
}


void ray_threads_destroy(ray_threads_t *threads)
{
	unsigned	i;

	for (i = 0; i < threads->n_threads; i++)
		pthread_cancel(threads->threads[i].thread);

	for (i = 0; i < threads->n_threads; i++)
		pthread_join(threads->threads[i].thread, NULL);

	free(threads);	
}
