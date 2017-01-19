#ifndef _RAY_COLOR_H
#define _RAY_COLOR_H

#include <stdint.h>

#include "ray_3f.h"

typedef ray_3f_t ray_color_t;

/* convert a vector into a packed, 32-bit rgb pixel value */
static inline uint32_t ray_color_to_uint32_rgb(ray_color_t color) {
	uint32_t	pixel;

	/* doing this all per-pixel, ugh. */

	if (color.x > 1.0f) color.x = 1.0f;
	if (color.y > 1.0f) color.y = 1.0f;
	if (color.z > 1.0f) color.z = 1.0f;

	pixel = (uint32_t)(color.x * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.y * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.z * 255.0f);

	return pixel;
}

#endif
