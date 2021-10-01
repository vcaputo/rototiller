#ifndef _HELPERS_H
#define _HELPERS_H

#include <stdint.h>

#include "til_fb.h"

#include "particle.h"
#include "particles.h"


/* helper for scaling rgb colors and packing them into an pixel */
static inline uint32_t makergb(uint32_t r, uint32_t g, uint32_t b, float intensity)
{
	r = (((float)intensity) * r);
	g = (((float)intensity) * g);
	b = (((float)intensity) * b);

	return (((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff));
}


/* return if the particle should be drawn, and set *longevity to 0 if out of bounds */
static inline int should_draw_expire_if_oob(particles_t *particles, particle_t *p, int x, int y, til_fb_fragment_t *f, int *longevity)
{
	if (!til_fb_fragment_contains(f, x, y)) {
		if (longevity && (x < 0 || x > f->frame_width || y < 0 || y > f->frame_height))
			*longevity = 0; /* offscreen */

		return 0;
	}

	return 1;
}

#endif
