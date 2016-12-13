#ifndef _RAY_THREADS_H
#define _RAY_THREADS_H

#include <pthread.h>

typedef struct ray_scene_t ray_scene_t;
typedef struct ray_camera_t ray_camera_t;
typedef struct fb_fragment_t fb_fragment_t;

typedef struct ray_thread_t {
	pthread_t	thread;
	pthread_mutex_t	mutex;
	pthread_cond_t	cond;
	ray_scene_t	*scene;
	ray_camera_t	*camera;
	fb_fragment_t	*fragment;
} ray_thread_t;

typedef struct ray_threads_t {
	unsigned	n_threads;
	ray_thread_t	threads[];
} ray_threads_t;


ray_threads_t * ray_threads_create(unsigned num);
void ray_threads_destroy(ray_threads_t *threads);

void ray_thread_fragment_submit(ray_thread_t *thread, ray_scene_t *scene, ray_camera_t *camera, fb_fragment_t *fragment);
void ray_thread_wait_idle(ray_thread_t *thread);
#endif
