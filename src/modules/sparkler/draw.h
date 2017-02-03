#ifndef _DRAW_H
#define _DRAW_H

#include <stdint.h>

#include "fb.h"


static inline void draw_pixel(fb_fragment_t *f, int x, int y, uint32_t pixel)
{
	uint32_t	*pixels = f->buf;

	/* FIXME this assumes stride is aligned to 4 */
	pixels[(y * (f->width + (f->stride >> 2))) + x] = pixel;
}

#endif
