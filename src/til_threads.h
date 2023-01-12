#ifndef _TIL_THREADS_H
#define _TIL_THREADS_H

typedef struct til_fb_fragment_t til_fb_fragment_t;
typedef struct til_threads_t til_threads_t;
typedef struct til_stream_t til_stream_t;

til_threads_t * til_threads_create();
void til_threads_destroy(til_threads_t *threads);

void til_threads_frame_submit(til_threads_t *threads, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *frame_plan, void (*render_fragment_func)(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr), til_module_context_t *context, til_stream_t *stream, unsigned ticks);
void til_threads_wait_idle(til_threads_t *threads);
unsigned til_threads_num_threads(til_threads_t *threads);

#endif
