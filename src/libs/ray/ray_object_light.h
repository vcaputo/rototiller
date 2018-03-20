#ifndef _RAY_OBJECT_LIGHT_H
#define _RAY_OBJECT_LIGHT_H

#include "ray_light_emitter.h"
#include "ray_object_type.h"


typedef struct ray_object_light_t {
	ray_object_type_t	type;
	float			brightness;
	ray_light_emitter_t	emitter;
} ray_object_light_t;

#endif
