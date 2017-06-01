#ifndef _RAY_SCENE_H
#define _RAY_SCENE_H

#include "fb.h"

#include "ray_camera.h"
#include "ray_color.h"
#include "ray_ray.h"

typedef union ray_object_t ray_object_t;

typedef struct ray_scene_t {
	ray_object_t		*objects;
	unsigned		n_objects;

	ray_object_t		*lights;
	unsigned		n_lights;

	ray_color_t		ambient_color;
	float			ambient_brightness;

	struct {
		ray_color_t	ambient_light;
	} _prepared;
} ray_scene_t;

void ray_scene_prepare(ray_scene_t *scene);
void ray_scene_render_fragment(ray_scene_t *scene, ray_camera_t *camera, fb_fragment_t *fragment);

#endif
