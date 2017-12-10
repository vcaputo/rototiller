#ifndef _RAY_SCENE_H
#define _RAY_SCENE_H

#include "ray_color.h"

typedef union ray_object_t ray_object_t;

typedef struct ray_scene_t {
	ray_object_t	*objects;
	ray_object_t	*lights;

	ray_color_t	ambient_color;
	float		ambient_brightness;
} ray_scene_t;

#endif
