#ifndef _RAY_OBJECT_H
#define _RAY_OBJECT_H

#include "ray_object_light.h"
#include "ray_object_plane.h"
#include "ray_object_point.h"
#include "ray_object_sphere.h"
#include "ray_object_type.h"

typedef union ray_object_t {
	ray_object_type_t	type;
	ray_object_sphere_t	sphere;
	ray_object_point_t	point;
	ray_object_plane_t	plane;
	ray_object_light_t	light;
} ray_object_t;

#endif
