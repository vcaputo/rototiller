#ifndef _DRAW_H
#define _DRAW_H

#include <stdint.h>

#include "fb.h"

/* helper for scaling rgb colors and packing them into an pixel */
static inline uint32_t makergb(uint32_t r, uint32_t g, uint32_t b, float intensity)
{
	r = (((float)intensity) * r);
	g = (((float)intensity) * g);
	b = (((float)intensity) * b);

	return (((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff));
}

static inline void draw_pixel(fb_fragment_t *f, int x, int y, uint32_t pixel)
{
	uint32_t	*pixels = f->buf;

	/* FIXME this assumes stride is aligned to 4 */
	pixels[(y * (f->width + (f->stride >> 2))) + x] = pixel;
}

#endif
