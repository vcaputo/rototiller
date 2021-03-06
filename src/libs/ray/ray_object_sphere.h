#ifndef _RAY_OBJECT_SPHERE_H
#define _RAY_OBJECT_SPHERE_H

#include "ray_3f.h"
#include "ray_object_type.h"
#include "ray_surface.h"


typedef struct ray_object_sphere_t {
	ray_object_type_t	type;
	ray_surface_t		surface;
	ray_3f_t		center;
	float			radius;
} ray_object_sphere_t;

#endif
