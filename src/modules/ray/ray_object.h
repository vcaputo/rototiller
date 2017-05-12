#ifndef _RAY_OBJECT_H
#define _RAY_OBJECT_H

#include "ray_object_light.h"
#include "ray_object_plane.h"
#include "ray_object_point.h"
#include "ray_object_sphere.h"
#include "ray_object_type.h"
#include "ray_ray.h"
#include "ray_surface.h"

typedef union ray_object_t {
	ray_object_type_t	type;
	ray_object_sphere_t	sphere;
	ray_object_point_t	point;
	ray_object_plane_t	plane;
	ray_object_light_t	light;
} ray_object_t;

void ray_object_prepare(ray_object_t *object);
int ray_object_intersects_ray(ray_object_t *object, ray_ray_t *ray, float *res_distance);
ray_3f_t ray_object_normal(ray_object_t *object, ray_3f_t *point);
ray_surface_t ray_object_surface(ray_object_t *object, ray_3f_t *point);

#endif
