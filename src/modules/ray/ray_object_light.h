#ifndef _RAY_OBJECT_LIGHT_H
#define _RAY_OBJECT_LIGHT_H

#include <assert.h>

#include "ray_light_emitter.h"
#include "ray_object_light.h"
#include "ray_object_point.h"
#include "ray_object_sphere.h"
#include "ray_object_type.h"
#include "ray_ray.h"
#include "ray_surface.h"


typedef struct ray_object_light_t {
	ray_object_type_t	type;
	float			brightness;
	ray_light_emitter_t	emitter;
} ray_object_light_t;


static void ray_object_light_prepare(ray_object_light_t *light)
{
}


/* TODO: point is really the only one I've implemented... */
static inline int ray_object_light_intersects_ray(ray_object_light_t *light, ray_ray_t *ray, float *res_distance)
{
	switch (light->emitter.type) {
	case RAY_LIGHT_EMITTER_TYPE_POINT:
		return ray_object_point_intersects_ray(&light->emitter.point, ray, res_distance);

	case RAY_LIGHT_EMITTER_TYPE_SPHERE:
		return ray_object_sphere_intersects_ray(&light->emitter.sphere, ray, res_distance);
	default:
		assert(0);
	}
}


static inline ray_3f_t ray_object_light_normal(ray_object_light_t *light, ray_3f_t *point)
{
	ray_3f_t	normal;

	/* TODO */
	switch (light->emitter.type) {
	case RAY_LIGHT_EMITTER_TYPE_SPHERE:
		return normal;

	case RAY_LIGHT_EMITTER_TYPE_POINT:
		return normal;
	default:
		assert(0);
	}
}


static inline ray_surface_t ray_object_light_surface(ray_object_light_t *light, ray_3f_t *point)
{
	switch (light->emitter.type) {
	case RAY_LIGHT_EMITTER_TYPE_SPHERE:
		return ray_object_sphere_surface(&light->emitter.sphere, point);

	case RAY_LIGHT_EMITTER_TYPE_POINT:
		return ray_object_point_surface(&light->emitter.point, point);
	default:
		assert(0);
	}
}

#endif
