#ifndef _RAY_OBJECT_POINT_H
#define _RAY_OBJECT_POINT_H

#include "ray_3f.h"
#include "ray_object_type.h"
#include "ray_ray.h"
#include "ray_surface.h"


typedef struct ray_object_point_t {
	ray_object_type_t	type;
	ray_surface_t		surface;
	ray_3f_t		center;
} ray_object_point_t;


static void ray_object_point_prepare(ray_object_point_t *point)
{
}


static inline int ray_object_point_intersects_ray(ray_object_point_t *point, ray_ray_t *ray, float *res_distance)
{
	/* TODO: determine a ray:point intersection */
	return 0;
}


static inline ray_3f_t ray_object_point_normal(ray_object_point_t *point, ray_3f_t *_point)
{
	ray_3f_t	normal;

	return normal;
}


static inline ray_surface_t ray_object_point_surface(ray_object_point_t *point, ray_3f_t *_point)
{
	return point->surface;
}

#endif
