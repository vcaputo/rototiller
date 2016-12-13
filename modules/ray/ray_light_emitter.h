#ifndef _RAY_LIGHT_EMITTER_H
#define _RAY_LIGHT_EMITTER_H

#include "ray_object_point.h"
#include "ray_object_sphere.h"

typedef enum ray_light_emitter_type_t {
	RAY_LIGHT_EMITTER_TYPE_SPHERE,
	RAY_LIGHT_EMITTER_TYPE_POINT,
} ray_light_emitter_type_t;

typedef union ray_light_emitter_t {
	ray_light_emitter_type_t	type;
	ray_object_sphere_t		sphere;
	ray_object_point_t		point;
} ray_light_emitter_t;

#endif
