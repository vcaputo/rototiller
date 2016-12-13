#ifndef _RAY_MATERIAL_H
#define _RAY_MATERIAL_H

#include "ray_3f.h"
#include "ray_color.h"

/* Surface properties we expect every object to be able to introspect */
typedef struct ray_surface_t {
	ray_color_t	color;
	float		specular;
	float		diffuse;
} ray_surface_t;

#endif
