#ifndef _TIL_FB_H
#define _TIL_FB_H

#include <stdint.h>
#include <string.h>

#include "til_settings.h"
#include "til_setup.h"

/* All renderers should target fb_fragment_t, which may or may not represent
 * a full-screen mmap.  Helpers are provided for subdividing fragments for
 * concurrent renderers.
 */
typedef struct til_fb_fragment_t {
	uint32_t	*buf;		/* pointer to the first pixel in the fragment */
	unsigned	x, y;		/* absolute coordinates of the upper left corner of this fragment */
	unsigned	width, height;	/* width and height of this fragment */
	unsigned	frame_width;	/* width of the frame this fragment is part of */
	unsigned	frame_height;	/* height of the frame this fragment is part of */
	unsigned	stride;		/* number of 32-bit words from the end of one row to the start of the next */
	unsigned	pitch;		/* number of 32-bit words separating y from y + 1, including any padding */
	unsigned	number;		/* this fragment's number as produced by fragmenting */
	unsigned	cleared:1;	/* if this fragment has been cleared since last flip */
} til_fb_fragment_t;

/* This is a page handle object for page flip submission/life-cycle.
 * Outside of fb_page_get()/fb_page_put(), you're going to be interested in
 * fb_fragment_t.  The fragment included here describes the whole page,
 * it may be divided via fb_fragment_divide().
 */
typedef struct til_fb_page_t {
	til_fb_fragment_t	fragment;
} til_fb_page_t;

typedef struct til_fb_t til_fb_t;

/* Supply this struct to fb_new() with the appropriate context */
typedef struct til_fb_ops_t {
	int	(*setup)(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);
	int	(*init)(const til_settings_t *settings, void **res_context);
	void	(*shutdown)(til_fb_t *fb, void *context);
	int	(*acquire)(til_fb_t *fb, void *context, void *page);
	void	(*release)(til_fb_t *fb, void *context);
	void *	(*page_alloc)(til_fb_t *fb, void *context, til_fb_page_t *res_page);
	int	(*page_free)(til_fb_t *fb, void *context, void *page);
	int	(*page_flip)(til_fb_t *fb, void *context, void *page);
} til_fb_ops_t;

til_fb_page_t * til_fb_page_get(til_fb_t *fb);
void til_fb_page_put(til_fb_t *fb, til_fb_page_t *page);
til_fb_t * til_fb_free(til_fb_t *fb);
void til_fb_get_put_pages_count(til_fb_t *fb, unsigned *count);
int til_fb_new(const til_fb_ops_t *ops, til_settings_t *settings, int n_pages, til_fb_t **res_fb);
void til_fb_rebuild(til_fb_t *fb);
void * til_fb_context(til_fb_t *fb);
int til_fb_flip(til_fb_t *fb);
void til_fb_fragment_divide(til_fb_fragment_t *fragment, unsigned n_fragments, til_fb_fragment_t fragments[]);
int til_fb_fragment_slice_single(const til_fb_fragment_t *fragment, unsigned n_fragments, unsigned num, til_fb_fragment_t *res_fragment);
int til_fb_fragment_tile_single(const til_fb_fragment_t *fragment, unsigned tile_size, unsigned num, til_fb_fragment_t *res_fragment);


/* checks if a coordinate is contained within a fragment */
static inline int til_fb_fragment_contains(til_fb_fragment_t *fragment, int x, int y)
{
	if (x < fragment->x || x >= fragment->x + fragment->width ||
	    y < fragment->y || y >= fragment->y + fragment->height)
		return 0;

	return 1;
}


/* puts a pixel into the fragment, no bounds checking is performed. */
static inline void til_fb_fragment_put_pixel_unchecked(til_fb_fragment_t *fragment, int x, int y, uint32_t pixel)
{
	uint32_t	*pixels = fragment->buf + (y - fragment->y) * fragment->pitch;

	pixels[x - fragment->x] = pixel;
}


/* puts a pixel into the fragment, bounds checking is performed with a draw performed return status */
static inline int til_fb_fragment_put_pixel_checked(til_fb_fragment_t *fragment, int x, int y, uint32_t pixel)
{
	if (!til_fb_fragment_contains(fragment, x, y))
		return 0;

	til_fb_fragment_put_pixel_unchecked(fragment, x, y, pixel);

	return 1;
}


/* fill a fragment with an arbitrary pixel */
static inline void til_fb_fragment_fill(til_fb_fragment_t *fragment, uint32_t pixel)
{
	uint32_t	*buf = fragment->buf;

	/* TODO: there should be a fast-path for non-divided fragments where there's no stride to skip */
	for (int y = 0; y < fragment->height; y++, buf += fragment->pitch) {
		/* TODO: this should use something memset-like for perf */
		for (int x = 0; x < fragment->width; x++)
			buf[x] = pixel;
	}
}


/* clear a fragment */
static inline void til_fb_fragment_clear(til_fb_fragment_t *fragment)
{
	if (fragment->cleared)
		return;

	til_fb_fragment_fill(fragment, 0);

	fragment->cleared = 1;
}

#endif
