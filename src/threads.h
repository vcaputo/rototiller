#ifndef _THREADS_H
#define _THREADS_H

typedef struct fb_fragment_t fb_fragment_t;
typedef struct rototiller_frame_t rototiller_frame_t;
typedef struct threads_t threads_t;

threads_t * threads_create();
void threads_destroy(threads_t *threads);

void threads_frame_submit(threads_t *threads, fb_fragment_t *fragment, rototiller_fragmenter_t fragmenter, void (*render_fragment_func)(void *context, unsigned ticks, unsigned cpu, fb_fragment_t *fragment), void *context, unsigned ticks);
void threads_wait_idle(threads_t *threads);
unsigned threads_num_threads(threads_t *threads);

#endif
