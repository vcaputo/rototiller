#ifndef _RAY_RENDER_OBJECT_H
#define _RAY_RENDER_OBJECT_H

#include <assert.h>

#include "ray_camera.h"
#include "ray_object.h"
#include "ray_object_type.h"
#include "ray_render_object_plane.h"
#include "ray_render_object_point.h"
#include "ray_render_object_sphere.h"
#include "ray_ray.h"
#include "ray_surface.h"

typedef union ray_render_object_t {
	ray_object_type_t		type;
	ray_render_object_sphere_t	sphere;
	ray_render_object_point_t	point;
	ray_render_object_plane_t	plane;
} ray_render_object_t;



/* Prepare an object for rendering.
 * If the object has any pre-calculating to do, this is where it happens.
 * The pre-calculated stuff is object-resident under a _prepared struct member.
 */
static inline ray_render_object_t ray_render_object_prepare(ray_object_t *object, ray_camera_t *camera)
{
	ray_render_object_t	prepared = { .type = object->type };

	switch (object->type) {
	case RAY_OBJECT_TYPE_SPHERE:
		prepared.sphere = ray_render_object_sphere_prepare(&object->sphere, camera);
		break;

	case RAY_OBJECT_TYPE_POINT:
		prepared.point = ray_render_object_point_prepare(&object->point, camera);
		break;

	case RAY_OBJECT_TYPE_PLANE:
		prepared.plane = ray_render_object_plane_prepare(&object->plane, camera);
		break;

	case RAY_OBJECT_TYPE_LIGHT:
		/* TODO */
		break;

	default:
		assert(0);
	}

	return prepared;
}


/* Determine if a ray intersects object.
 * If the object is intersected, store where along the ray the intersection occurs in res_distance.
 */
static inline int ray_render_object_intersects_ray(ray_render_object_t *object, unsigned depth, ray_ray_t *ray, float *res_distance)
{
	switch (object->type) {
	case RAY_OBJECT_TYPE_SPHERE:
		return ray_render_object_sphere_intersects_ray(&object->sphere, depth, ray, res_distance);

	case RAY_OBJECT_TYPE_POINT:
		return ray_render_object_point_intersects_ray(&object->point, depth, ray, res_distance);

	case RAY_OBJECT_TYPE_PLANE:
		return ray_render_object_plane_intersects_ray(&object->plane, depth, ray, res_distance);

	case RAY_OBJECT_TYPE_LIGHT:
		/* TODO */
	default:
		assert(0);
	}
}


/* Return the surface normal of object @ point */
static inline ray_3f_t ray_render_object_normal(ray_render_object_t *object, ray_3f_t *point)
{
	switch (object->type) {
	case RAY_OBJECT_TYPE_SPHERE:
		return ray_render_object_sphere_normal(&object->sphere, point);

	case RAY_OBJECT_TYPE_POINT:
		return ray_render_object_point_normal(&object->point, point);

	case RAY_OBJECT_TYPE_PLANE:
		return ray_render_object_plane_normal(&object->plane, point);

	case RAY_OBJECT_TYPE_LIGHT:
		/* TODO */
	default:
		assert(0);
	}
}


/* Return the surface of object @ point */
static inline ray_surface_t ray_render_object_surface(ray_render_object_t *object, ray_3f_t *point)
{
	switch (object->type) {
	case RAY_OBJECT_TYPE_SPHERE:
		return ray_render_object_sphere_surface(&object->sphere, point);

	case RAY_OBJECT_TYPE_POINT:
		return ray_render_object_point_surface(&object->point, point);

	case RAY_OBJECT_TYPE_PLANE:
		return ray_render_object_plane_surface(&object->plane, point);

	case RAY_OBJECT_TYPE_LIGHT:
		/* TODO */
	default:
		assert(0);
	}
}

#endif
