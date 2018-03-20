#ifndef _RAY_RENDER_OBJECT_PLANE_H
#define _RAY_RENDER_OBJECT_PLANE_H

#include "ray_3f.h"
#include "ray_camera.h"
#include "ray_object_plane.h"
#include "ray_object_type.h"
#include "ray_ray.h"
#include "ray_surface.h"


typedef struct ray_render_object_plane_t {
	ray_object_plane_t	object;
	float			primary_dot_plus;
} ray_render_object_plane_t;


static ray_render_object_plane_t ray_render_object_plane_prepare(const ray_object_plane_t *plane, const ray_camera_t *camera)
{
	ray_render_object_plane_t	prepared = { .object = *plane };

	prepared.primary_dot_plus = ray_3f_dot(&plane->normal, &camera->position) + plane->distance;

	return prepared;
}


static inline int ray_render_object_plane_intersects_ray(ray_render_object_plane_t *plane, unsigned depth, ray_ray_t *ray, float *res_distance)
{
	float	d = ray_3f_dot(&plane->object.normal, &ray->direction);

	if (d < 0.0f) {
		float	distance = plane->primary_dot_plus;

		if (depth)
			distance = (ray_3f_dot(&plane->object.normal, &ray->origin) + plane->object.distance);

		distance /= -d;
		if (distance > 0.0f) {
			*res_distance = distance;

			return 1;
		}
	}

	return 0;
}


static inline ray_3f_t ray_render_object_plane_normal(ray_render_object_plane_t *plane, ray_3f_t *point)
{
	return plane->object.normal;
}


static inline ray_surface_t ray_render_object_plane_surface(ray_render_object_plane_t *plane, ray_3f_t *point)
{
	return plane->object.surface;
}

#endif
