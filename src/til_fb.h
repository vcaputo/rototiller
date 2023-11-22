#ifndef _TIL_FB_H
#define _TIL_FB_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "til_settings.h"
#include "til_setup.h"
#include "til_util.h"

typedef struct til_fb_fragment_t til_fb_fragment_t;
typedef struct til_fb_fragment_ops_t til_fb_fragment_ops_t;

#define TIL_FB_DRAW_FLAG_TEXTURABLE	0x1

/* All renderers should target fb_fragment_t, which may or may not represent
 * a full-screen mmap.  Helpers are provided for subdividing fragments for
 * concurrent renderers.
 */
typedef struct til_fb_fragment_t {
	const til_fb_fragment_ops_t	*ops;		/* optional opaque ops for physical fragments, NULL for strictly logical fragments */

	til_fb_fragment_t		*texture;	/* optional source texture when drawing to this fragment */
	uint32_t			*buf;		/* pointer to the first pixel in the fragment */
	unsigned			x, y;		/* absolute coordinates of the upper left corner of this fragment */
	unsigned			width, height;	/* width and height of this fragment */
	unsigned			frame_width;	/* width of the frame this fragment is part of */
	unsigned			frame_height;	/* height of the frame this fragment is part of */
	unsigned			stride;		/* number of 32-bit words from the end of one row to the start of the next */
	unsigned			pitch;		/* number of 32-bit words separating y from y + 1, including any padding */
	unsigned			number;		/* this fragment's number as produced by fragmenting */
	unsigned			cleared:1;	/* if this fragment has been cleared since last flip */
} til_fb_fragment_t;

typedef struct til_fb_t til_fb_t;

/* Supply this struct to fb_new() with the appropriate context */
typedef struct til_fb_ops_t {
	int	(*setup)(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);
	int	(*init)(const char *title, const til_setup_t *setup, void **res_context);
	void	(*shutdown)(til_fb_t *fb, void *context);
	int	(*acquire)(til_fb_t *fb, void *context, void *page);
	void	(*release)(til_fb_t *fb, void *context);
	void *	(*page_alloc)(til_fb_t *fb, void *context, til_fb_fragment_t *res_page_fragment);
	int	(*page_free)(til_fb_t *fb, void *context, void *page);
	int	(*page_flip)(til_fb_t *fb, void *context, void *page);
} til_fb_ops_t;

til_fb_fragment_t * til_fb_page_get(til_fb_t *fb, unsigned *res_delay_ticks);
void til_fb_fragment_submit(til_fb_fragment_t *fragment);
til_fb_fragment_t * til_fb_fragment_snapshot(til_fb_fragment_t **fragment_ptr, int preserve_original);
til_fb_fragment_t * til_fb_fragment_reclaim(til_fb_fragment_t *fragment);
til_fb_t * til_fb_free(til_fb_t *fb);
void til_fb_get_put_pages_count(til_fb_t *fb, unsigned *count);
int til_fb_new(const til_fb_ops_t *ops, const char *title, const til_setup_t *setup, int n_pages, til_fb_t **res_fb);
void til_fb_rebuild(til_fb_t *fb);
void til_fb_halt(til_fb_t *fb);
void * til_fb_context(til_fb_t *fb);
int til_fb_flip(til_fb_t *fb);
void til_fb_fragment_divide(til_fb_fragment_t *fragment, unsigned n_fragments, til_fb_fragment_t fragments[]);
int til_fb_fragment_noop_single(const til_fb_fragment_t *fragment, unsigned n_fragments, unsigned num, til_fb_fragment_t *res_fragment);
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


/* gets a pixel from the fragment, no bounds checking is performed. */
static inline uint32_t til_fb_fragment_get_pixel_unchecked(til_fb_fragment_t *fragment, int x, int y)
{
	return fragment->buf[(y - fragment->y) * fragment->pitch + x - fragment->x];
}


/* gets a pixel from the fragment, coordinates are clipped to the fragment's bounds */
static inline uint32_t til_fb_fragment_get_pixel_clipped(til_fb_fragment_t *fragment, int x, int y)
{
	/* XXX: this originally clipped to the fragment's frame bounds, but checkers introduced
	 * a practice of shifting edge fragments to center the filled checkers, while preserving the
	 * whole frame size.  So if something like fill_module=drizzle got used, and the edge cells
	 * were clipped by the fragment while the frame hung off the edge, the snapshot would be of
	 * strictly the fragment with the logical frame size running off the buffer, resulting in a
	 * segfault.  It's slightly complicated in that drizzle::style=map would actually appreciate
	 * being able to access pixels outside the fragment, but that's only really remotely safe when
	 * the snapshot it's operating on is a full page snapshot - not the fragment-sized copy-based
	 * snapshot you get from something like checkers::fill_module=drizzle.  So for now, this is
	 * simply getting clipped to the fragment bounds, which is the simple and correct thing to do.
	 * For drizzle to be able to sample outside its given fragment, it's just not viable today as-is,
	 * let's just stop the crashing for now.
	 */
	if (x < fragment->x)
		x = fragment->x;
	else if (x >= fragment->x + fragment->width)
		x = fragment->x + fragment->width - 1;

	if (y < fragment->y)
		y = fragment->y;
	else if (y >= fragment->y + fragment->height)
		y = fragment->y + fragment->height - 1;

	return til_fb_fragment_get_pixel_unchecked(fragment, x, y);
}


/* puts a pixel into the fragment, no bounds checking is performed. */
static inline void til_fb_fragment_put_pixel_unchecked(til_fb_fragment_t *fragment, uint32_t flags, int x, int y, uint32_t pixel)
{
	if (fragment->texture && (flags & TIL_FB_DRAW_FLAG_TEXTURABLE))
		pixel = til_fb_fragment_get_pixel_unchecked(fragment->texture, x, y);

	fragment->buf[(y - fragment->y) * fragment->pitch + x - fragment->x] = pixel;
}


/* puts a pixel into the fragment, bounds checking is performed with a draw performed return status */
static inline int til_fb_fragment_put_pixel_checked(til_fb_fragment_t *fragment, uint32_t flags, int x, int y, uint32_t pixel)
{
	if (!til_fb_fragment_contains(fragment, x, y))
		return 0;

	til_fb_fragment_put_pixel_unchecked(fragment, flags, x, y, pixel);

	return 1;
}


/* copy a fragment, x,y,width,height are absolute coordinates within the frames, and will be clipped to the overlapping fragment areas */
static inline void til_fb_fragment_copy(til_fb_fragment_t *dest, uint32_t flags, int x, int y, int width, int height, til_fb_fragment_t *src)
{
	int	X = MAX(MAX(dest->x, src->x), x);
	int	Y = MAX(MAX(dest->y, src->y), y);
	int	W = MIN(MIN(dest->x + dest->width, src->x + src->width), x + width) - X;
	int	H = MIN(MIN(dest->y + dest->height, src->y + src->height), y + height) - Y;

	if (W <= 0 || H <= 0)
		return;

	/* XXX FIXME TODO */
	/* XXX FIXME TODO */
	/* XXX FIXME TODO */
	/* this is PoC fast and nasty code, optimize this to at least bulk copy rows of pixels */
	/* XXX FIXME TODO */
	/* XXX FIXME TODO */
	/* XXX FIXME TODO */
	for (int v = 0; v < H; v++) {
		for (int u = 0; u < W; u++)
			til_fb_fragment_put_pixel_unchecked(dest, flags, X + u, Y + v, til_fb_fragment_get_pixel_unchecked(src, X + u, Y + v));
	}
}


static inline void _til_fb_fragment_fill(til_fb_fragment_t *fragment, uint32_t pixel)
{
	uint32_t	*buf = fragment->buf;

	/* TODO: there should be a fast-path for non-divided fragments where there's no stride to skip */
	for (int y = 0; y < fragment->height; y++, buf += fragment->pitch) {
		if (!pixel) {
			memset(buf, pixel, fragment->width * sizeof(pixel));
		} else {
			/* TODO: this should use something memset-like for perf */
			for (int x = 0; x < fragment->width; x++)
				buf[x] = pixel;
		}
	}
}


/* fill a fragment with an arbitrary pixel */
static inline void til_fb_fragment_fill(til_fb_fragment_t *fragment, uint32_t flags, uint32_t pixel)
{
	if (!(fragment->texture && (flags & TIL_FB_DRAW_FLAG_TEXTURABLE)))
		return _til_fb_fragment_fill(fragment, pixel);

	/* when a texture is present, pixel is ignored and instead sourced from fragment->texture->buf[y*pitch+x] */
	til_fb_fragment_copy(fragment, flags, fragment->x, fragment->y, fragment->width, fragment->height, fragment->texture);
}


/* clear a fragment */
static inline void til_fb_fragment_clear(til_fb_fragment_t *fragment)
{
	if (fragment->cleared)
		return;

	_til_fb_fragment_fill(fragment, 0);

	fragment->cleared = 1;
}

#endif
