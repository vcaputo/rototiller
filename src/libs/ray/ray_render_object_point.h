#ifndef _RAY_RENDER_OBJECT_POINT_H
#define _RAY_RENDER_OBJECT_POINT_H

#include "ray_3f.h"
#include "ray_camera.h"
#include "ray_object_point.h"
#include "ray_object_type.h"
#include "ray_ray.h"
#include "ray_surface.h"


typedef struct ray_render_object_point_t {
	ray_object_point_t	object;
} ray_render_object_point_t;


static ray_render_object_point_t ray_render_object_point_prepare(const ray_object_point_t *point, const ray_camera_t *camera)
{
	ray_render_object_point_t	prepared = { .object = *point };

	return prepared;
}


static inline int ray_render_object_point_intersects_ray(ray_render_object_point_t *point, unsigned depth, ray_ray_t *ray, float *res_distance)
{
	/* TODO: determine a ray:point intersection */
	return 0;
}


static inline ray_3f_t ray_render_object_point_normal(ray_render_object_point_t *point, ray_3f_t *_point)
{
	ray_3f_t	normal = {};

	return normal;
}


static inline ray_surface_t ray_render_object_point_surface(ray_render_object_point_t *point, ray_3f_t *_point)
{
	return point->object.surface;
}

#endif
