#ifndef _RAY_OBJECT_PLANE_H
#define _RAY_OBJECT_PLANE_H

#include "ray_3f.h"
#include "ray_camera.h"
#include "ray_object_type.h"
#include "ray_ray.h"
#include "ray_surface.h"


typedef struct ray_object_plane_t {
	ray_object_type_t	type;
	ray_surface_t		surface;
	ray_3f_t		normal;
	float			distance;
} ray_object_plane_t;


static void ray_object_plane_prepare(ray_object_plane_t *plane, ray_camera_t *camera)
{
}


static inline int ray_object_plane_intersects_ray(ray_object_plane_t *plane, unsigned depth, ray_ray_t *ray, float *res_distance)
{
	float	d = ray_3f_dot(&plane->normal, &ray->direction);

	if (d >= 0.00001f) {
		float	distance = (ray_3f_dot(&plane->normal, &ray->origin) + plane->distance) / d;

		if (distance > 0) {
			*res_distance = distance;

			return 1;
		}
	}

	return 0;
}


static inline ray_3f_t ray_object_plane_normal(ray_object_plane_t *plane, ray_3f_t *point)
{
	return plane->normal;
}


static inline ray_surface_t ray_object_plane_surface(ray_object_plane_t *plane, ray_3f_t *point)
{
	return plane->surface;
}

#endif
