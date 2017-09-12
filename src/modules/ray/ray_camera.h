#ifndef _RAY_CAMERA_H
#define _RAY_CAMERA_H

#include <math.h>

#include "fb.h"

#include "ray_3f.h"
#include "ray_euler.h"
#include "ray_ray.h"


typedef struct ray_camera_t {
	ray_3f_t	position;		/* position of camera, the origin of all its rays */
	ray_euler_t	orientation;		/* orientation of the camera */
	float		focal_length;		/* controls the field of view */
	unsigned	width;			/* width of camera viewport in pixels */
	unsigned	height;			/* height of camera viewport in pixels */
} ray_camera_t;


typedef struct ray_camera_frame_t {
	ray_camera_t	*camera;		/* the camera supplied to frame_begin() */
	fb_fragment_t	*fragment;		/* the fragment supplied to frame_begin() */
	ray_ray_t	*ray;			/* the ray supplied to frame_begin(), which gets updated as we step through the frame. */

	ray_3f_t	nw, ne, sw, se;		/* directions pointing through the corners of the frame fragment */
	ray_3f_t	cur_w, cur_e;		/* current row's west and east ends */
	float		x_alpha, y_alpha;	/* interpolation position along the x and y axis */
	float		x_delta, y_delta;	/* interpolation step delta along the x and y axis */
	unsigned	x, y;			/* integral position within frame fragment */
} ray_camera_frame_t;


void ray_camera_frame_begin(ray_camera_t *camera, fb_fragment_t *fragment, ray_ray_t *ray, ray_camera_frame_t *frame);


/* Step the ray through the frame on the x axis, returns 1 when rays remain on this axis, 0 at the end. */
/* When 1 is returned, frame->ray is left pointing through the new coordinate. */
static inline int ray_camera_frame_x_step(ray_camera_frame_t *frame)
{
	frame->x++;

	if (frame->x >= frame->fragment->width) {
		frame->x = 0;
		frame->x_alpha = frame->x_delta * (float)frame->fragment->x;
		return 0;
	}

	frame->x_alpha += frame->x_delta;
	frame->ray->direction = ray_3f_nlerp(&frame->cur_w, &frame->cur_e, frame->x_alpha);

	return 1;
}


/* Step the ray through the frame on the y axis, returns 1 when rays remain on this axis, 0 at the end. */
/* When 1 is returned, frame->ray is left pointing through the new coordinate. */
static inline int ray_camera_frame_y_step(ray_camera_frame_t *frame)
{
	frame->y++;

	if (frame->y >= frame->fragment->height) {
		frame->y = 0;
		frame->y_alpha = frame->y_delta * (float)frame->fragment->y;
		return 0;
	}

	frame->y_alpha += frame->y_delta;
	frame->cur_w = ray_3f_lerp(&frame->nw, &frame->sw, frame->y_alpha);
	frame->cur_e = ray_3f_lerp(&frame->ne, &frame->se, frame->y_alpha);
	frame->ray->direction = ray_3f_nlerp(&frame->cur_w, &frame->cur_e, frame->x_alpha);

	return 1;
}

#endif
