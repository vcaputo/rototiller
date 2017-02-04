#ifndef _FB_H
#define _FB_H

#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <xf86drmMode.h> /* for drmModeModeInfoPtr */

/* All renderers should target fb_fragment_t, which may or may not represent
 * a full-screen mmap.  Helpers are provided for subdividing fragments for
 * concurrent renderers.
 */
typedef struct fb_fragment_t {
	uint32_t	*buf;		/* pointer to the first pixel in the fragment */
	unsigned	x, y;		/* absolute coordinates of the upper left corner of this fragment */
	unsigned	width, height;	/* width and height of this fragment */
	unsigned	stride;		/* number of bytes from the end of one row to the start of the next */
} fb_fragment_t;

/* This is a page handle object for page flip submission/life-cycle.
 * Outside of fb_page_get()/fb_page_put(), you're going to be interested in
 * fb_fragment_t.  The fragment included here describes the whole page,
 * it may be divided via fb_fragment_divide().
 */
typedef struct fb_page_t {
	fb_fragment_t	fragment;
} fb_page_t;

typedef struct fb_t fb_t;

fb_page_t * fb_page_get(fb_t *fb);
void fb_page_put(fb_t *fb, fb_page_t *page);
void fb_free(fb_t *fb);
void fb_get_put_pages_count(fb_t *fb, unsigned *count);
fb_t * fb_new(int drm_fd, uint32_t crtc_id, uint32_t *connectors, int n_connectors, drmModeModeInfoPtr mode, int n_pages);
void fb_fragment_divide(fb_fragment_t *fragment, unsigned n_fragments, fb_fragment_t fragments[]);


/* checks if a coordinate is contained within a fragment */
static inline int fb_fragment_contains(fb_fragment_t *fragment, int x, int y)
{
	if (x < fragment->x || x >= fragment->x + fragment->width ||
	    y < fragment->y || y >= fragment->y + fragment->height)
		return 0;

	return 1;
}


/* puts a pixel into the fragment, no bounds checking is performed. */
static inline void fb_fragment_put_pixel_unchecked(fb_fragment_t *fragment, int x, int y, uint32_t pixel)
{
	uint32_t	*pixels = fragment->buf;

	/* FIXME this assumes stride is aligned to 4 */
	pixels[(y * (fragment->width + (fragment->stride >> 2))) + x] = pixel;
}


/* puts a pixel into the fragment, bounds checking is performed with a draw performed return status */
static inline int fb_fragment_put_pixel_checked(fb_fragment_t *fragment, int x, int y, uint32_t pixel)
{
	if (!fb_fragment_contains(fragment, x, y))
		return 0;

	fb_fragment_put_pixel_unchecked(fragment, x, y, pixel);

	return 1;
}


/* zero a fragment */
static inline void fb_fragment_zero(fb_fragment_t *fragment)
{
	memset(fragment->buf, 0, ((fragment->width << 2) + fragment->stride) * fragment->height);
}

#endif
