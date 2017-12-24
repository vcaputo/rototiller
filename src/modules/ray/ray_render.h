#ifndef _RAY_RENDER_H
#define _RAY_RENDER_H

#include "fb.h"

#include "ray_camera.h"
#include "ray_scene.h"

typedef struct ray_render_t ray_render_t;

ray_render_t * ray_render_new(ray_scene_t *scene, ray_camera_t *camera);
void ray_render_free(ray_render_t *render);
void ray_render_trace_fragment(ray_render_t *render, fb_fragment_t *fb_fragment);

#endif