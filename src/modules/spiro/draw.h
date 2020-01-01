#ifndef _DRAW_H
#define _DRAW_H

#include <stdint.h>

/* helper for scaling rgb colors and packing them into an pixel */
static inline uint32_t makergb(uint32_t r, uint32_t g, uint32_t b, float intensity)
{
	r = (((float)intensity) * r);
	g = (((float)intensity) * g);
	b = (((float)intensity) * b);

	return (((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff));
}

#endif
