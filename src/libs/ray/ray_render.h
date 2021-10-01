#ifndef _RAY_RENDER_H
#define _RAY_RENDER_H

#include "til_fb.h"

#include "ray_camera.h"
#include "ray_scene.h"

typedef struct ray_render_t ray_render_t;

ray_render_t * ray_render_new(const ray_scene_t *scene, const ray_camera_t *camera, unsigned frame_width, unsigned frame_height);
void ray_render_free(ray_render_t *render);
void ray_render_trace_fragment(ray_render_t *render, til_fb_fragment_t *fb_fragment);

#endif
