#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#include "fb.h"
#include "util.h"

/* Copyright (C) 2016-2017 Vito Caputo <vcaputo@pengaru.com> */


/* I've used a separate thread for page-flipping duties because the libdrm api
 * (and related kernel ioctl) for page flips doesn't appear to support queueing
 * multiple flip requests.  In this use case we aren't interactive and wish to
 * just accumulate rendered pages until we run out of spare pages, allowing the
 * renderer to get as far ahead of vsync as possible, and certainly never
 * blocked waiting for vsync unless there's no spare page available for drawing
 * into.
 *
 * In lieu of a queueing mechanism on the drm fd, we must submit the next page
 * once the currently submitted page is flipped to - it's at that moment we
 * won't get EBUSY from the ioctl any longer.  Without a dedicated thread
 * submitting flip requests and synchronously consuming their flip events,
 * we're liable to introduce latency in the page flip submission if implemented
 * in a more opportunistic manner whenever the fb api is entered from the
 * render loop.
 *
 * If the kernel simply let us queue multiple flip requests we could maintain
 * our submission queue entirely in the drm fd, and get available pages from
 * the drm event handler once our pool of pages is depleted.  The kernel on
 * vsync could check the fd to see if another flip is queued and there would be
 * the least latency possible in submitting the flips - the least likely to
 * miss a vsync.  This would also elide the need for synchronization in
 * userspace between the renderer and the flipper thread, since there would no
 * longer be a flipper thread.
 *
 * Let me know if you're aware of a better way with existing mainline drm!
 */


/* Most of fb_page_t is kept private, the public part is
 * just an fb_fragment_t describing the whole page.
 */
typedef struct _fb_page_t _fb_page_t;
struct _fb_page_t {
	void		*ops_page;

	_fb_page_t	*next;
	fb_page_t	public_page;
};

typedef struct fb_t {
	const fb_ops_t	*ops;
	void		*ops_context;

	pthread_t	thread;

	_fb_page_t	*active_page;		/* page currently displayed */

	pthread_mutex_t	ready_mutex;
	pthread_cond_t	ready_cond;
	_fb_page_t	*ready_pages_head;	/* next pages to flip to */
	_fb_page_t	*ready_pages_tail;

	pthread_mutex_t	inactive_mutex;
	pthread_cond_t	inactive_cond;
	_fb_page_t	*inactive_pages;	/* finished pages available for (re)use */

	unsigned	put_pages_count;
} fb_t;

#ifndef container_of
#define container_of(_ptr, _type, _member) \
	(_type *)((void *)(_ptr) - offsetof(_type, _member))
#endif


/* Consumes ready pages queued via fb_page_put(), submits them to drm to flip
 * on vsync.  Produces inactive pages from those replaced, making them
 * available to fb_page_get(). */
static void * fb_flipper_thread(void *_fb)
{
	fb_t	*fb = _fb;

	for (;;) {
		_fb_page_t	*next_active_page;
		/* wait for a flip req, submit the req page for flip on vsync, wait for it to flip before making the
		 * active page inactive/available, repeat.
		 */
		pthread_mutex_lock(&fb->ready_mutex);
		while (!fb->ready_pages_head)
			pthread_cond_wait(&fb->ready_cond, &fb->ready_mutex);

		next_active_page = fb->ready_pages_head;
		fb->ready_pages_head = next_active_page->next;
		if (!fb->ready_pages_head)
			fb->ready_pages_tail = NULL;
		pthread_mutex_unlock(&fb->ready_mutex);

		/* submit the next active page for page flip on vsync, and wait for it. */
		pexit_if(fb->ops->page_flip(fb->ops_context, next_active_page->ops_page) < 0,
			"unable to flip page");

		/* now that we're displaying a new page, make the previously active one inactive so rendering can reuse it */
		pthread_mutex_lock(&fb->inactive_mutex);
		fb->active_page->next = fb->inactive_pages;
		fb->inactive_pages = fb->active_page;
		pthread_cond_signal(&fb->inactive_cond);
		pthread_mutex_unlock(&fb->inactive_mutex);

		fb->active_page = next_active_page;
	}
}


/* acquire the fb, making page the visible page */
static int fb_acquire(fb_t *fb, _fb_page_t *page)
{
	int	ret;

	ret = fb->ops->acquire(fb->ops_context, page->ops_page);
	if (ret < 0)
		return ret;

	fb->active_page = page;

	/* start up the page flipper thread */
	pthread_create(&fb->thread, NULL, fb_flipper_thread, fb);

	return 0;
}


/* release the fb, making the visible page inactive */
static void fb_release(fb_t *fb)
{
	pthread_cancel(fb->thread);
	pthread_join(fb->thread, NULL);

	fb->ops->release(fb->ops_context);
	fb->active_page->next = fb->inactive_pages;
	fb->inactive_pages = fb->active_page;
	fb->active_page = NULL;
}


/* creates a framebuffer page */
static void fb_page_new(fb_t *fb)
{
	_fb_page_t	*page;

	page = calloc(1, sizeof(_fb_page_t));
	assert(page);

	page->ops_page = fb->ops->page_alloc(fb->ops_context, &page->public_page);

	pthread_mutex_lock(&fb->inactive_mutex);
	page->next = fb->inactive_pages;
	fb->inactive_pages = page;
	pthread_mutex_unlock(&fb->inactive_mutex);

}


static void _fb_page_free(fb_t *fb, _fb_page_t *page)
{
	fb->ops->page_free(fb->ops_context, page->ops_page);

	free(page);
}


/* get the next inactive page from the fb, waiting if necessary. */
static inline _fb_page_t * _fb_page_get(fb_t *fb)
{
	_fb_page_t	*page;

	/* As long as n_pages is >= 3 this won't block unless we're submitting
	 * pages faster than vhz.
	 */
	pthread_mutex_lock(&fb->inactive_mutex);
	while (!(page = fb->inactive_pages))
		pthread_cond_wait(&fb->inactive_cond, &fb->inactive_mutex);
	fb->inactive_pages = page->next;
	pthread_mutex_unlock(&fb->inactive_mutex);

	page->next = NULL;

	return page;
}


/* public interface */
fb_page_t * fb_page_get(fb_t *fb)
{
	return &(_fb_page_get(fb)->public_page);
}


/* put a page into the fb, queueing for display */
static inline void _fb_page_put(fb_t *fb, _fb_page_t *page)
{
	pthread_mutex_lock(&fb->ready_mutex);
	if (fb->ready_pages_tail)
		fb->ready_pages_tail->next = page;
	else
		fb->ready_pages_head = page;

	fb->ready_pages_tail = page;
	pthread_cond_signal(&fb->ready_cond);
	pthread_mutex_unlock(&fb->ready_mutex);
}


/* public interface */

/* put a page into the fb, queueing for display */
void fb_page_put(fb_t *fb, fb_page_t *page)
{
	fb->put_pages_count++;

	_fb_page_put(fb, container_of(page, _fb_page_t, public_page));
}


/* get (and reset) the current count of put pages */
void fb_get_put_pages_count(fb_t *fb, unsigned *count)
{
	*count = fb->put_pages_count;
	fb->put_pages_count = 0;
}


/* free the fb and associated resources */
void fb_free(fb_t *fb)
{
	if (fb->active_page)
		fb_release(fb);

	/* TODO: free all the pages */

	pthread_mutex_destroy(&fb->ready_mutex);
	pthread_cond_destroy(&fb->ready_cond);
	pthread_mutex_destroy(&fb->inactive_mutex);
	pthread_cond_destroy(&fb->inactive_cond);

	free(fb);
}


/* create a new fb instance */
fb_t * fb_new(const fb_ops_t *ops, void *context, int n_pages)
{
	_fb_page_t	*page;
	fb_t		*fb;
	int		i;

	assert(ops);
	assert(ops->page_alloc);
	assert(ops->page_free);
	assert(ops->page_flip);
	assert(n_pages > 1);

	/* XXX: page-flipping is the only supported rendering model, requiring 2+ pages. */
	if (n_pages < 2)
		return NULL;

	fb = calloc(1, sizeof(fb_t));
	if (!fb)
		return NULL;

	fb->ops = ops;
	fb->ops_context = context;

	for (i = 0; i < n_pages; i++)
		fb_page_new(fb);

	pthread_mutex_init(&fb->ready_mutex, NULL);
	pthread_cond_init(&fb->ready_cond, NULL);
	pthread_mutex_init(&fb->inactive_mutex, NULL);
	pthread_cond_init(&fb->inactive_cond, NULL);

	page = _fb_page_get(fb);
	if (!page)
		goto fail;

	if (fb_acquire(fb, page) < 0)
		goto fail;

	return fb;

fail:
	fb_free(fb);

	return NULL;
}


/* helpers for fragmenting incrementally */
int fb_fragment_slice_single(const fb_fragment_t *fragment, unsigned n_fragments, unsigned num, fb_fragment_t *res_fragment)
{
	unsigned	slice = fragment->height / n_fragments;
	unsigned	yoff = slice * num;
	unsigned	pitch;

	if (yoff >= fragment->height)
		return 0;

	pitch = (fragment->width * 4) + fragment->stride;

	res_fragment->buf = ((void *)fragment->buf) + yoff * pitch;
	res_fragment->x = fragment->x;
	res_fragment->y = yoff;
	res_fragment->width = fragment->width;
	res_fragment->height = MIN(fragment->height - yoff, slice);
	res_fragment->frame_width = fragment->frame_width;
	res_fragment->frame_height = fragment->frame_height;
	res_fragment->stride = fragment->stride;

	return 1;
}


int fb_fragment_tile_single(const fb_fragment_t *fragment, unsigned tile_size, unsigned num, fb_fragment_t *res_fragment)
{
	unsigned	w = fragment->width / tile_size, h = fragment->height / tile_size;
	unsigned	pitch = (fragment->width * 4) + fragment->stride;
	unsigned	x, y, xoff, yoff;

	if (w * tile_size < fragment->width)
		w++;

	if (h * tile_size < fragment->height)
		h++;

	y = num / w;
	if (y >= h)
		return 0;

	x = num - (y * w);

	xoff = x * tile_size;
	yoff = y * tile_size;

	res_fragment->buf = (void *)fragment->buf + (yoff * pitch) + (xoff * 4);
	res_fragment->x = fragment->x + xoff;
	res_fragment->y = fragment->y + yoff;
	res_fragment->width = MIN(fragment->width - xoff, tile_size);
	res_fragment->height = MIN(fragment->height - yoff, tile_size);
	res_fragment->frame_width = fragment->frame_width;
	res_fragment->frame_height = fragment->frame_height;
	res_fragment->stride = fragment->stride + ((fragment->width - res_fragment->width) * 4);

	return 1;
}
