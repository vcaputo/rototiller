#ifndef _RAY_GAMMA_H
#define _RAY_GAMMA_H

#include <stdint.h>

#include "ray_color.h"

typedef struct ray_gamma_t {
	float	gamma;
	uint8_t	table[1024];
} ray_gamma_t;

void ray_gamma_prepare(float gamma, ray_gamma_t *res_gamma);

/* convert a color into a gamma-corrected, packed, 32-bit rgb pixel value */
static inline uint32_t ray_gamma_color_to_uint32_rgb(ray_gamma_t *gamma, ray_color_t color) {
	uint32_t	pixel;

	if (color.x > 1.0f)
		color.x = 1.0f;

	if (color.y > 1.0f)
		color.y = 1.0f;

	if (color.z > 1.0f)
		color.z = 1.0f;

	pixel = (uint32_t)gamma->table[(unsigned)floorf(1023.0f * color.x)];
	pixel <<= 8;
	pixel |= (uint32_t)gamma->table[(unsigned)floorf(1023.0f * color.y)];
	pixel <<= 8;
	pixel |= (uint32_t)gamma->table[(unsigned)floorf(1023.0f * color.z)];

	return pixel;
}

#endif
