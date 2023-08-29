#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#include "til.h"
#include "til_fb.h"
#include "til_settings.h"
#include "til_util.h"

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
 *
 *
 * XXX: til_fb_new() used to create a thread which did the equivalent of til_fb_flip()
 * continuously in a loop.  This posed a problem for the sdl_fb backend, due to
 * the need for event pumping in the page flip hook.  SDL internally uses TLS
 * and requires that the same thread which initialized SDL call the event
 * functions.  To satisfy this requirement, the body of the flipper thread loop
 * has been moved to the til_fb_flip() function.  Rototiller's main thread is
 * expected to call this repeatedly, turning it effectively into the flipper
 * thread.  This required rototiller to move what was previously the main
 * thread's duties - page rendering dispatch, to a separate thread.
 */

typedef struct _til_fb_fragment_t _til_fb_fragment_t;

struct til_fb_fragment_ops_t {
	void			(*submit)(til_fb_fragment_t *fragment);
	til_fb_fragment_t *	(*snapshot)(til_fb_fragment_t **fragment_ptr, int preserve_original);
	void			(*reclaim)(til_fb_fragment_t *fragment);
};

struct _til_fb_fragment_t {
	til_fb_fragment_t	public;
	til_fb_fragment_ops_t	ops;
};
/* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * The private fragment groups ops with the public fragment, so for the physical
 * fragments for pages produced here, the fragment->ops pointer will point to the
 * appropriately initialized ops member in the private _til_fb_fragment_t.
 *
 * For ad-hoc/logical fragments constructed outside of here (i.e. by fragmenters),
 * the public til_fb_fragment_ops_t is just an opaque forward declaration.
 * It's expected those cases will leave ops NULL and when such fragments get passed
 * into here we'll know they don't have any of the capabilities encapsulated in
 * the ops.  There will also be some functions here like frame submission that will
 * treat receiving a fragment without any .ops or .ops.submit() as a fatal programming
 * error by asserting.
 *
 * It might make sense to at some point better separate the frame- oriented
 * fragment API from the fragment-oriented one, since the frame-oriented stuff
 * is primarily of relevance to front-end authors, and the purely
 * fragment-oriented stuff is what module authors are interested in.  I.e.
 * module authors want to be able to efficiently snapshot/reclaim fragments, but
 * front-ends want to be able to submit whole-frame fragments (pages).  It's a
 * bit murky to have that all grouped under til_fb_fragment_ops_t and everything
 * operating on a seemingly universal til_fb_fragment_t type despite the
 * disparity of capabilities.  Polymorphism is sort of more a neat word than an
 * actually desirable thing in practice.
 *
 * The main reason the page and fragment have become conflated behind
 * til_fb_fragment_t to then be disambiguated by the API implementation via an
 * opaque ops member is when a fragment gets snapshotted by a module, it needs
 * to be able to potentially swap out the destination page if possible, for
 * efficiency reasons.  The fragment_ptr passed everywhere being _the_ handle
 * for the destination page is a convenient way to make this happen, but it
 * might make more sense to just pass around the fragment as before and instead
 * add a concept of a page handle the fragment can optionally point back at when
 * it's the root fragment for the page.  There are some details to manage here
 * in that the root fragment for the page is page-specific, in that things like
 * pitch/stride can vary page-to-page and obviously buf will point at memory
 * bound to the backing page.  So with a page backreference in the fragment, the
 * snapshot would have to not only manipulate/swap-out the backreferenced page,
 * but it would still have to rebuild the fragment itself for the new page.
 * When everything is fragment_ptr-centric, it seems much more natural to do
 * this and reason about what will happen to the underlying fragment contents in
 * the event of an optimized snapshot...  I think this needs more consideration
 * and refinement in any event.
 */

/* Most of til_fb_page_t is kept private, the public part is
 * just an til_fb_fragment_t describing the whole page.
 */
typedef struct _til_fb_page_t _til_fb_page_t;
struct _til_fb_page_t {
	til_fb_t		*fb;
	void			*fb_ops_page;

	_til_fb_page_t		*all_next, *all_previous;
	_til_fb_page_t		*next, *previous;
	_til_fb_fragment_t	fragment;
	unsigned		submitted_ticks, presented_ticks;
};

typedef struct til_fb_t {
	const til_fb_ops_t	*ops;
	void			*ops_context;
	int			n_pages;

	pthread_mutex_t		rebuild_mutex;
	int			rebuild_pages;		/* counter of pages needing a rebuild */

	_til_fb_page_t		*active_page;		/* page currently displayed */

	pthread_mutex_t		ready_mutex;
	pthread_cond_t		ready_cond;
	_til_fb_page_t		*ready_pages_head;	/* next pages to flip to */
	_til_fb_page_t		*ready_pages_tail;

	pthread_mutex_t		inactive_mutex;
	pthread_cond_t		inactive_cond;
	_til_fb_page_t		*inactive_pages_head;	/* finished pages available for (re)use */
	_til_fb_page_t		*inactive_pages_tail;

	_til_fb_page_t		*all_pages_head;	/* all pages allocated */
	_til_fb_page_t		*all_pages_tail;

	unsigned		put_pages_count;
	unsigned		halted:1;
} til_fb_t;

#ifndef container_of
#define container_of(_ptr, _type, _member) \
	(_type *)((void *)(_ptr) - offsetof(_type, _member))
#endif

static _til_fb_page_t * _til_fb_page_alloc(til_fb_t *fb);


/* Consumes ready pages queued via til_fb_page_put(), submits them to drm to flip
 * on vsync.  Produces inactive pages from those replaced, making them
 * available to til_fb_page_get(). */
int til_fb_flip(til_fb_t *fb)
{
	_til_fb_page_t	*next_active_page;
	int		r;

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
	r = fb->ops->page_flip(fb, fb->ops_context, next_active_page->fb_ops_page);
	if (r < 0)	/* TODO: vet this: what happens to this page? */
		return r;

	next_active_page->presented_ticks = til_ticks_now();

	/* now that we're displaying a new page, make the previously active one inactive so rendering can reuse it */
	pthread_mutex_lock(&fb->inactive_mutex);
	fb->active_page->next = fb->inactive_pages_head;
	fb->inactive_pages_head = fb->active_page;
	fb->inactive_pages_head->previous = NULL;
	if (fb->inactive_pages_head->next)
		fb->inactive_pages_head->next->previous = fb->inactive_pages_head;
	else
		fb->inactive_pages_tail = fb->inactive_pages_head;

	/* before setting the renderer loose, check if there's more page rebuilding needed,
	 * and if there is do as much as possible here in the inactive set.  Note it's important
	 * that the renderer take pages from the tail, and we always replenish inactive at the
	 * head, as well as rebuild pages from the head.
	 */
	pthread_mutex_lock(&fb->rebuild_mutex);
	for (_til_fb_page_t *p = fb->inactive_pages_head; p && fb->rebuild_pages > 0; p = p->next) {
		fb->ops->page_free(fb, fb->ops_context, p->fb_ops_page);
		p->fb_ops_page = fb->ops->page_alloc(fb, fb->ops_context, &p->fragment.public);
		p->fragment.public.ops = &p->fragment.ops;
		fb->rebuild_pages--;
	}
	pthread_mutex_unlock(&fb->rebuild_mutex);

	pthread_cond_signal(&fb->inactive_cond);
	pthread_mutex_unlock(&fb->inactive_mutex);

	fb->active_page = next_active_page;

	return 0;
}


/* acquire the fb, making page the visible page */
static int til_fb_acquire(til_fb_t *fb, _til_fb_page_t *page)
{
	int	ret;

	ret = fb->ops->acquire(fb, fb->ops_context, page->fb_ops_page);
	if (ret < 0)
		return ret;

	fb->active_page = page;

	return 0;
}


/* release the fb, making the visible page inactive */
static void til_fb_release(til_fb_t *fb)
{
	assert(fb);
	assert(fb->active_page);

	fb->ops->release(fb, fb->ops_context);

	/* XXX: this is getting silly, either add a doubly linked list header or
	 * at least use some functions for this local to this file.
	 */
	fb->active_page->next = fb->inactive_pages_head;
	fb->inactive_pages_head = fb->active_page;
	fb->inactive_pages_head->previous = NULL;
	if (fb->inactive_pages_head->next)
		fb->inactive_pages_head->next->previous = fb->inactive_pages_head;
	else
		fb->inactive_pages_tail = fb->inactive_pages_head;

	fb->active_page = NULL;
}


static void _til_fb_page_free(til_fb_t *fb, _til_fb_page_t *page)
{
	if (page->all_next)
		page->all_next->all_previous = page->all_previous;
	else
		fb->all_pages_tail = page->all_previous;

	if (page->all_previous)
		page->all_previous->all_next = page->all_next;
	else
		fb->all_pages_head = page->all_next;

	fb->ops->page_free(fb, fb->ops_context, page->fb_ops_page);

	free(page);
}


/* submit the page backing fragment into the fb, queueing for display */
static void _til_fb_page_submit(til_fb_fragment_t *fragment)
{
	_til_fb_page_t	*page = container_of(fragment, _til_fb_page_t, fragment);
	til_fb_t	*fb = page->fb;

	fb->put_pages_count++;

	page->submitted_ticks = til_ticks_now();
	pthread_mutex_lock(&fb->ready_mutex);
	if (fb->ready_pages_tail)
		fb->ready_pages_tail->next = page;
	else
		fb->ready_pages_head = page;

	fb->ready_pages_tail = page;
	pthread_cond_signal(&fb->ready_cond);
	pthread_mutex_unlock(&fb->ready_mutex);
}


/* reclaim the page backing fragment back to the fb */
static void _til_fb_page_reclaim(til_fb_fragment_t *fragment)
{
	_til_fb_page_t	*page = container_of(fragment, _til_fb_page_t, fragment);

	_til_fb_page_free(page->fb, page);
}


/* bare helper for copying fragment contents */
static void _til_fb_fragment_memcpy_buf(til_fb_fragment_t *dest, til_fb_fragment_t *src)
{
	assert(dest->width == src->width);
	assert(dest->height == src->height);

	for (unsigned y = 0; y < dest->height; y++)
		memcpy(&dest->buf[y * dest->pitch], &src->buf[y * src->pitch], dest->width * sizeof(uint32_t));
}


/* snapshot the contents of whole-page fragment */
static til_fb_fragment_t * _til_fb_page_snapshot(til_fb_fragment_t **fragment_ptr, int preserve_original)
{
	_til_fb_page_t	*page;
	_til_fb_page_t	*new_page;

	assert(fragment_ptr && *fragment_ptr);

	/* XXX: note that nothing serializes this _til_fb_page_alloc(), so if anything actually
	 * tried to do whole-page snapshotting in parallel things would likely explode.
	 * But as of now, all parallel snapshots of fragments occur on subfragments, not on the
	 * top-level page, so they don't enter this page allocation code path.
	 *
	 * If things started doing threaded page allocations/snapshots, it'd break some assumptions
	 * all the way down to the $foo_fb backends where they maintain spare pages lists without
	 * any locking. XXX
	 */
	page = container_of(*fragment_ptr, _til_fb_page_t, fragment);
	new_page = _til_fb_page_alloc(page->fb);
	*fragment_ptr = &new_page->fragment.public;

	if (preserve_original)
		_til_fb_fragment_memcpy_buf(&new_page->fragment.public, &page->fragment.public);

	return &page->fragment.public;
}


/* allocate a framebuffer page */
static _til_fb_page_t * _til_fb_page_alloc(til_fb_t *fb)
{
	_til_fb_page_t	*page;

	page = calloc(1, sizeof(_til_fb_page_t));
	assert(page);

	page->fb = fb;
	page->fb_ops_page = fb->ops->page_alloc(fb, fb->ops_context, &page->fragment.public);
	assert(page->fb_ops_page);
	page->fragment.ops.submit = _til_fb_page_submit;
	page->fragment.ops.snapshot = _til_fb_page_snapshot;
	page->fragment.ops.reclaim = _til_fb_page_reclaim;
	page->fragment.public.ops = &page->fragment.ops;

	page->all_next = fb->all_pages_head;
	fb->all_pages_head = page;
	if (page->all_next)
		page->all_next->all_previous = page;
	else
		fb->all_pages_tail = page;

	return page;
}


/* creates a framebuffer page, leaving it in the inactive pages list */
static void _til_fb_page_new(til_fb_t *fb)
{
	_til_fb_page_t	*page;

	page = _til_fb_page_alloc(fb);

	pthread_mutex_lock(&fb->inactive_mutex);
	page->next = fb->inactive_pages_head;
	fb->inactive_pages_head = page;
	if (fb->inactive_pages_head->next)
		fb->inactive_pages_head->next->previous = fb->inactive_pages_head;
	else
		fb->inactive_pages_tail = fb->inactive_pages_head;
	pthread_mutex_unlock(&fb->inactive_mutex);

}


/* get the next inactive page from the fb, waiting if necessary. */
static inline _til_fb_page_t * _til_fb_page_get(til_fb_t *fb)
{
	_til_fb_page_t	*page;

	/* As long as n_pages is >= 3 this won't block unless we're submitting
	 * pages faster than vhz.
	 */
	pthread_mutex_lock(&fb->inactive_mutex);
	while (!(page = fb->inactive_pages_tail) && !fb->halted)
		pthread_cond_wait(&fb->inactive_cond, &fb->inactive_mutex);

	if (!page) {
		pthread_mutex_unlock(&fb->inactive_mutex);
		return NULL;
	}

	fb->inactive_pages_tail = page->previous;
	if (fb->inactive_pages_tail)
		fb->inactive_pages_tail->next = NULL;
	else
		fb->inactive_pages_head = NULL;
	pthread_mutex_unlock(&fb->inactive_mutex);

	page->next = page->previous = NULL;
	page->fragment.public.cleared = 0;

	return page;
}


/* public interface */
til_fb_fragment_t * til_fb_page_get(til_fb_t *fb, unsigned *res_delay_ticks)
{
	_til_fb_page_t	*page;

	page = _til_fb_page_get(fb);
	if (!page)
		return NULL;

	if (res_delay_ticks) {
		/* TODO: handle overflows, just asserting for now until it rears its head */
		assert(page->presented_ticks >= page->submitted_ticks);
		*res_delay_ticks = page->presented_ticks - page->submitted_ticks;
	}

	return &page->fragment.public;
}


/* submit the page backing the supplied whole-page fragment to the fb, queueing for display */
void til_fb_fragment_submit(til_fb_fragment_t *fragment)
{
	/* XXX: There's no actual need to locate the submit() method via the
	 * fragment; we could just call _til_fb_fragment_submit() here.
	 * But by only initializing ops.submit for full-page fragments, we
	 * can at least prevent submission on non-page fragments.  So just
	 * go through that circuit here, maybe one day the functions used
	 * might even vary per-backend.
	 */
	assert(fragment->ops && fragment->ops->submit);
	fragment->ops->submit(fragment);
}


static void _til_fb_snapshot_reclaim(til_fb_fragment_t *fragment)
{
	assert(fragment);
	assert(fragment->buf);

	free(fragment->buf);
	free(fragment);
}


/* Snapshot the fragment, returning the snapshot, updating *fragment_ptr if necessary.
 * The remaining contents of *fragment_ptr->buf are undefined if preserve_original=0.
 * The returned snapshot will always contain the original contents of *fragment_ptr->buf.
 */
til_fb_fragment_t * til_fb_fragment_snapshot(til_fb_fragment_t **fragment_ptr, int preserve_original)
{
	_til_fb_fragment_t	*_fragment;

	assert(fragment_ptr && *fragment_ptr);

	/* when there's a snapshot method just let it do some magic */
	if ((*fragment_ptr)->ops && (*fragment_ptr)->ops->snapshot)
		return (*fragment_ptr)->ops->snapshot(fragment_ptr, preserve_original);

	/* otherwise we just allocate a new fragment, and copy *fragment_ptr->buf to it */
	/* unfortunately this must always incur the cost of preserving the original fragment's contents */
	_fragment = calloc(1, sizeof(_til_fb_fragment_t));
	assert(_fragment);
	_fragment->public.frame_width = (*fragment_ptr)->frame_width;
	_fragment->public.frame_height = (*fragment_ptr)->frame_height;
	_fragment->public.pitch = _fragment->public.width = (*fragment_ptr)->width;
	_fragment->public.height = (*fragment_ptr)->height;
	_fragment->public.x = (*fragment_ptr)->x;
	_fragment->public.y = (*fragment_ptr)->y;
	_fragment->public.buf = malloc(_fragment->public.width * _fragment->public.height * sizeof(uint32_t));
	assert(_fragment->public.buf);
	_fragment->ops.reclaim = _til_fb_snapshot_reclaim;
	_fragment->public.ops = &_fragment->ops;

	_til_fb_fragment_memcpy_buf(&_fragment->public, (*fragment_ptr));

	return &_fragment->public;
}


/* reclaim the fragment (for cleaning up snapshots) */
til_fb_fragment_t * til_fb_fragment_reclaim(til_fb_fragment_t *fragment)
{
	assert(fragment);

	if (fragment->ops && fragment->ops->reclaim)
		fragment->ops->reclaim(fragment);

	return NULL;
}


/* get (and reset) the current count of put pages */
void til_fb_get_put_pages_count(til_fb_t *fb, unsigned *count)
{
	*count = fb->put_pages_count;
	fb->put_pages_count = 0;
}


/* free the fb and associated resources */
til_fb_t * til_fb_free(til_fb_t *fb)
{
	if (fb) {
		int	count = 0;

		if (fb->active_page)
			til_fb_release(fb);

		while (fb->all_pages_head) {
			_til_fb_page_free(fb, fb->all_pages_head);
			count++;
		}

		assert(count == fb->n_pages);

		if (fb->ops->shutdown && fb->ops_context)
			fb->ops->shutdown(fb, fb->ops_context);

		pthread_mutex_destroy(&fb->ready_mutex);
		pthread_cond_destroy(&fb->ready_cond);
		pthread_mutex_destroy(&fb->inactive_mutex);
		pthread_cond_destroy(&fb->inactive_cond);

		free(fb);
	}

	return NULL;
}


/* create a new fb instance */
int til_fb_new(const til_fb_ops_t *ops, const char *title, const til_setup_t *setup, int n_pages, til_fb_t **res_fb)
{
	_til_fb_page_t	*page;
	til_fb_t		*fb;
	int		r;

	assert(ops);
	assert(ops->page_alloc);
	assert(ops->page_free);
	assert(ops->page_flip);
	assert(n_pages > 1);
	assert(res_fb);

	/* XXX: page-flipping is the only supported rendering model, requiring 2+ pages. */
	if (n_pages < 2)
		return -EINVAL;

	fb = calloc(1, sizeof(til_fb_t));
	if (!fb)
		return -ENOMEM;

	fb->ops = ops;
	if (ops->init) {
		r = ops->init(title, setup, &fb->ops_context);
		if (r < 0)
			goto fail;
	}

	for (int i = 0; i < n_pages; i++)
		_til_fb_page_new(fb);

	fb->n_pages = n_pages;

	pthread_mutex_init(&fb->ready_mutex, NULL);
	pthread_cond_init(&fb->ready_cond, NULL);
	pthread_mutex_init(&fb->inactive_mutex, NULL);
	pthread_cond_init(&fb->inactive_cond, NULL);
	pthread_mutex_init(&fb->rebuild_mutex, NULL);

	page = _til_fb_page_get(fb);
	if (!page) {
		r = -ENOMEM;
		goto fail;
	}

	r = til_fb_acquire(fb, page);
	if (r < 0)
		goto fail;

	*res_fb = fb;

	return r;

fail:
	til_fb_free(fb);

	return r;
}


/* This informs the fb to reconstruct its pages as they become inactive,
 * giving the backend an opportunity to reconfigure them before they get
 * rendered to again.  It's intended to be used in response to window
 * resizes.
 */
void til_fb_rebuild(til_fb_t *fb)
{
	assert(fb);

	/* TODO: this could easily be an atomic counter since we have no need for waiting */
	pthread_mutex_lock(&fb->rebuild_mutex);
	fb->rebuild_pages = fb->n_pages;
	pthread_mutex_unlock(&fb->rebuild_mutex);
}


void til_fb_halt(til_fb_t *fb)
{
	assert(fb);

	fb->halted = 1;
	pthread_cond_signal(&fb->inactive_cond);
}


/* accessor for getting the ops_context */
void * til_fb_context(til_fb_t *fb)
{
	assert(fb);

	return fb->ops_context;
}


/* helpers for fragmenting incrementally */
int til_fb_fragment_slice_single(const til_fb_fragment_t *fragment, unsigned n_fragments, unsigned number, til_fb_fragment_t *res_fragment)
{
	unsigned	slice = MAX(fragment->height / n_fragments, 1);
	unsigned	yoff = slice * number;

	assert(fragment);
	assert(res_fragment);

	if (yoff >= fragment->height)
		return 0;

	if (fragment->texture) {
		assert(res_fragment->texture);
		assert(fragment->frame_width == fragment->texture->frame_width);
		assert(fragment->frame_height == fragment->texture->frame_height);
		assert(fragment->width == fragment->texture->width);
		assert(fragment->height == fragment->texture->height);
		assert(fragment->x == fragment->texture->x);
		assert(fragment->y == fragment->texture->y);

		*(res_fragment->texture) = (til_fb_fragment_t){
				.buf = fragment->texture->buf + yoff * fragment->texture->pitch,
				.x = fragment->x,
				.y = fragment->y + yoff,
				.width = fragment->width,
				.height = MIN(fragment->height - yoff, slice),
				.frame_width = fragment->frame_width,
				.frame_height = fragment->frame_height,
				.stride = fragment->texture->stride,
				.pitch = fragment->texture->pitch,
				.cleared = fragment->texture->cleared,
		};

	}

	*res_fragment = (til_fb_fragment_t){
				.texture = fragment->texture ? res_fragment->texture : NULL,
				.buf = fragment->buf + yoff * fragment->pitch,
				.x = fragment->x,
				.y = fragment->y + yoff,
				.width = fragment->width,
				.height = MIN(fragment->height - yoff, slice),
				.frame_width = fragment->frame_width,
				.frame_height = fragment->frame_height,
				.stride = fragment->stride,
				.pitch = fragment->pitch,
				.number = number,
				.cleared = fragment->cleared,
			};

	return 1;
}


int til_fb_fragment_tile_single(const til_fb_fragment_t *fragment, unsigned tile_size, unsigned number, til_fb_fragment_t *res_fragment)
{
	unsigned	w = fragment->width / tile_size, h = fragment->height / tile_size;
	unsigned	x, y, xoff, yoff;

	assert(fragment);
	assert(res_fragment);

	if (w * tile_size < fragment->width)
		w++;

	if (h * tile_size < fragment->height)
		h++;

	y = number / w;
	if (y >= h)
		return 0;

	x = number - (y * w);

	xoff = x * tile_size;
	yoff = y * tile_size;

	if (fragment->texture) {
		assert(res_fragment->texture);
		assert(fragment->frame_width == fragment->texture->frame_width);
		assert(fragment->frame_height == fragment->texture->frame_height);
		assert(fragment->width == fragment->texture->width);
		assert(fragment->height == fragment->texture->height);
		assert(fragment->x == fragment->texture->x);
		assert(fragment->y == fragment->texture->y);

		*(res_fragment->texture) = (til_fb_fragment_t){
					.buf = fragment->texture->buf + (yoff * fragment->texture->pitch) + (xoff),
					.x = fragment->x + xoff,
					.y = fragment->y + yoff,
					.width = MIN(fragment->width - xoff, tile_size),
					.height = MIN(fragment->height - yoff, tile_size),
					.frame_width = fragment->frame_width,
					.frame_height = fragment->frame_height,
					.stride = fragment->texture->stride + (fragment->width - MIN(fragment->width - xoff, tile_size)),
					.pitch = fragment->texture->pitch,
					.cleared = fragment->texture->cleared,
				};

	}

	*res_fragment = (til_fb_fragment_t){
				.texture = fragment->texture ? res_fragment->texture : NULL,
				.buf = fragment->buf + (yoff * fragment->pitch) + (xoff),
				.x = fragment->x + xoff,
				.y = fragment->y + yoff,
				.width = MIN(fragment->width - xoff, tile_size),
				.height = MIN(fragment->height - yoff, tile_size),
				.frame_width = fragment->frame_width,
				.frame_height = fragment->frame_height,
				.stride = fragment->stride + (fragment->width - MIN(fragment->width - xoff, tile_size)),
				.pitch = fragment->pitch,
				.number = number,
				.cleared = fragment->cleared,
			};

	return 1;
}
