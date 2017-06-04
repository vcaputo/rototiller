#ifndef _RAY_OBJECT_SPHERE_H
#define _RAY_OBJECT_SPHERE_H

#include <math.h>

#include "ray_3f.h"
#include "ray_camera.h"
#include "ray_color.h"
#include "ray_object_type.h"
#include "ray_ray.h"
#include "ray_surface.h"


typedef struct ray_object_sphere_t {
	ray_object_type_t	type;
	ray_surface_t		surface;
	ray_3f_t		center;
	float			radius;
	struct {
		ray_3f_t	primary_v;
		float		primary_dot_vv;
		float		r2;
		float		r_inv;
	} _prepared;
} ray_object_sphere_t;


static void ray_object_sphere_prepare(ray_object_sphere_t *sphere, ray_camera_t *camera)
{
	sphere->_prepared.primary_v = ray_3f_sub(&camera->position, &sphere->center);
	sphere->_prepared.primary_dot_vv = ray_3f_dot(&sphere->_prepared.primary_v, &sphere->_prepared.primary_v);

	sphere->_prepared.r2 = sphere->radius * sphere->radius;

	/* to divide by radius via multiplication in ray_object_sphere_normal() */
	sphere->_prepared.r_inv = 1.0f / sphere->radius;
}


static inline int ray_object_sphere_intersects_ray(ray_object_sphere_t *sphere, unsigned depth, ray_ray_t *ray, float *res_distance)
{
	ray_3f_t	v = sphere->_prepared.primary_v;
	float		dot_vv = sphere->_prepared.primary_dot_vv;
	float		b, disc;

	if (depth) {
		v = ray_3f_sub(&ray->origin, &sphere->center);
		dot_vv = ray_3f_dot(&v, &v);
	}

	b = ray_3f_dot(&v, &ray->direction);
	disc = sphere->_prepared.r2 - (dot_vv - (b * b));
	if (disc > 0) {
		float	i1, i2;

		disc = sqrtf(disc);

		i1 = b - disc;
		i2 = b + disc;

		if (i2 > 0 && i1 > 0) {
			*res_distance = i1;
			return 1;
		}
	}

	return 0;
}


/* return the normal of the surface at the specified point */
static inline ray_3f_t ray_object_sphere_normal(ray_object_sphere_t *sphere, ray_3f_t *point)
{
	ray_3f_t	normal;
	
	normal = ray_3f_sub(point, &sphere->center);
	normal = ray_3f_mult_scalar(&normal, sphere->_prepared.r_inv);	/* normalize without the sqrt() */

	return normal;
}


/* return the surface of the sphere @ point */
static inline ray_surface_t ray_object_sphere_surface(ray_object_sphere_t *sphere, ray_3f_t *point)
{
	/* uniform solids for now... */
	return sphere->surface;
}

#endif
