#ifndef _RAY_LIGHT_EMITTER_H
#define _RAY_LIGHT_EMITTER_H

#include "ray_object_point.h"
#include "ray_object_sphere.h"

typedef enum ray_light_emitter_type_t {
	RAY_LIGHT_EMITTER_TYPE_SENTINEL, /* TODO: this is fragile, the enum values align with ray_object_type_t so the object-specific .type can be assigned as object types to silence compiler warnings, without producing wrong type values in the ray_light_emitter_t.type.  What should probably be done is splitting out the rudimentary objects into even simpler structs without a type member so we can have light variants and object variants each with their own enum typed type members...  not now. */
	RAY_LIGHT_EMITTER_TYPE_SPHERE,
	RAY_LIGHT_EMITTER_TYPE_POINT,
} ray_light_emitter_type_t;

typedef union ray_light_emitter_t {
	ray_light_emitter_type_t	type;
	ray_object_sphere_t		sphere;
	ray_object_point_t		point;
} ray_light_emitter_t;

#endif
