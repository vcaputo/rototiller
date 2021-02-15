#ifndef _FB_H
#define _FB_H

#include <stdint.h>
#include <string.h>

#include "settings.h"

/* All renderers should target fb_fragment_t, which may or may not represent
 * a full-screen mmap.  Helpers are provided for subdividing fragments for
 * concurrent renderers.
 */
typedef struct fb_fragment_t {
	uint32_t	*buf;		/* pointer to the first pixel in the fragment */
	unsigned	x, y;		/* absolute coordinates of the upper left corner of this fragment */
	unsigned	width, height;	/* width and height of this fragment */
	unsigned	frame_width;	/* width of the frame this fragment is part of */
	unsigned	frame_height;	/* height of the frame this fragment is part of */
	unsigned	stride;		/* number of bytes from the end of one row to the start of the next */
	unsigned	pitch;		/* number of bytes separating y from y + 1, including any padding */
	unsigned	number;		/* this fragment's number as produced by fragmenting */
	unsigned	zeroed:1;	/* if this fragment has been zeroed since last flip */
} fb_fragment_t;

/* This is a page handle object for page flip submission/life-cycle.
 * Outside of fb_page_get()/fb_page_put(), you're going to be interested in
 * fb_fragment_t.  The fragment included here describes the whole page,
 * it may be divided via fb_fragment_divide().
 */
typedef struct fb_page_t {
	fb_fragment_t	fragment;
} fb_page_t;

/* Supply this struct to fb_new() with the appropriate context */
typedef struct fb_ops_t {
	int	(*setup)(const settings_t *settings, setting_desc_t **next);
	int	(*init)(const settings_t *settings, void **res_context);
	void	(*shutdown)(void *context);
	int	(*acquire)(void *context, void *page);
	void	(*release)(void *context);
	void *	(*page_alloc)(void *context, fb_page_t *res_page);
	int	(*page_free)(void *context, void *page);
	int	(*page_flip)(void *context, void *page);
} fb_ops_t;

typedef struct fb_t fb_t;

fb_page_t * fb_page_get(fb_t *fb);
void fb_page_put(fb_t *fb, fb_page_t *page);
void fb_free(fb_t *fb);
void fb_get_put_pages_count(fb_t *fb, unsigned *count);
int fb_new(const fb_ops_t *ops, settings_t *settings, int n_pages, fb_t **res_fb);
int fb_flip(fb_t *fb);
void fb_fragment_divide(fb_fragment_t *fragment, unsigned n_fragments, fb_fragment_t fragments[]);
int fb_fragment_slice_single(const fb_fragment_t *fragment, unsigned n_fragments, unsigned num, fb_fragment_t *res_fragment);
int fb_fragment_tile_single(const fb_fragment_t *fragment, unsigned tile_size, unsigned num, fb_fragment_t *res_fragment);


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
	uint32_t	*pixels = ((void *)fragment->buf) + (y - fragment->y) * fragment->pitch;

	pixels[x - fragment->x] = pixel;
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
	void	*buf = fragment->buf;

	if (fragment->zeroed)
		return;

	/* TODO: there should be a fast-path for non-divided fragments where there's no stride to skip */
	for (int y = 0; y < fragment->height; y++, buf += fragment->pitch)
		memset(buf, 0, fragment->pitch - fragment->stride);

	fragment->zeroed = 1;
}

#endif
