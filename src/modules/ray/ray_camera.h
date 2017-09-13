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

	ray_3f_t	nw, ne, sw, se;		/* directions pointing through the corners of the frame fragment */
	float		x_delta, y_delta;	/* interpolation step delta along the x and y axis */
} ray_camera_frame_t;


typedef struct ray_camera_fragment_t {
	ray_camera_frame_t	*frame;			/* the frame supplied to fragment_begin() */
	fb_fragment_t		*fb_fragment;		/* the fragment supplied to fragment_begin() */
	ray_ray_t		*ray;			/* the ray supplied to frame_begin(), which gets updated as we step through the frame. */

	ray_3f_t		cur_w, cur_e;		/* current row's west and east ends */
	float			x_alpha, y_alpha;	/* interpolation position along the x and y axis */
	unsigned		x, y;			/* integral position within frame fragment */
} ray_camera_fragment_t;


void ray_camera_frame_prepare(ray_camera_t *camera, ray_camera_frame_t *res_frame);
void ray_camera_fragment_begin(ray_camera_frame_t *frame, fb_fragment_t *fb_fragment, ray_ray_t *res_ray, ray_camera_fragment_t *res_fragment);


/* Step the ray through the fragment on the x axis, returns 1 when rays remain on this axis, 0 at the end. */
/* When 1 is returned, fragment->ray is left pointing through the new coordinate. */
static inline int ray_camera_fragment_x_step(ray_camera_fragment_t *fragment)
{
	fragment->x++;

	if (fragment->x >= fragment->fb_fragment->width) {
		fragment->x = 0;
		fragment->x_alpha = fragment->frame->x_delta * (float)fragment->fb_fragment->x;
		return 0;
	}

	fragment->x_alpha += fragment->frame->x_delta;
	fragment->ray->direction = ray_3f_nlerp(&fragment->cur_w, &fragment->cur_e, fragment->x_alpha);

	return 1;
}


/* Step the ray through the fragment on the y axis, returns 1 when rays remain on this axis, 0 at the end. */
/* When 1 is returned, fragment->ray is left pointing through the new coordinate. */
static inline int ray_camera_fragment_y_step(ray_camera_fragment_t *fragment)
{
	fragment->y++;

	if (fragment->y >= fragment->fb_fragment->height) {
		fragment->y = 0;
		fragment->y_alpha = fragment->frame->y_delta * (float)fragment->fb_fragment->y;
		return 0;
	}

	fragment->y_alpha += fragment->frame->y_delta;
	fragment->cur_w = ray_3f_lerp(&fragment->frame->nw, &fragment->frame->sw, fragment->y_alpha);
	fragment->cur_e = ray_3f_lerp(&fragment->frame->ne, &fragment->frame->se, fragment->y_alpha);
	fragment->ray->direction = ray_3f_nlerp(&fragment->cur_w, &fragment->cur_e, fragment->x_alpha);

	return 1;
}

#endif
