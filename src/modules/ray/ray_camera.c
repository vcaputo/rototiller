#include "fb.h"

#include "ray_camera.h"
#include "ray_euler.h"


/* Produce a vector from the provided orientation vectors and proportions. */
static ray_3f_t project_corner(ray_3f_t *forward, ray_3f_t *left, ray_3f_t *up, float focal_length, float horiz, float vert)
{
	ray_3f_t	tmp;
	ray_3f_t	corner;

	corner = ray_3f_mult_scalar(forward, focal_length);
	tmp = ray_3f_mult_scalar(left, horiz);
	corner = ray_3f_add(&corner, &tmp);
	tmp = ray_3f_mult_scalar(up, vert);
	corner = ray_3f_add(&corner, &tmp);

	return ray_3f_normalize(&corner);
}


/* Produce vectors for the corners of the entire camera frame, used for interpolation. */
static void project_corners(ray_camera_t *camera, ray_camera_frame_t *frame)
{
	ray_3f_t	forward, left, up, right, down;
	float		half_horiz = (float)camera->width * 0.5f;
	float		half_vert = (float)camera->height * 0.5f;

	ray_euler_basis(&camera->orientation, &forward, &up, &left);
	right = ray_3f_negate(&left);
	down = ray_3f_negate(&up);

	frame->nw = project_corner(&forward, &left, &up, camera->focal_length, half_horiz, half_vert);
	frame->ne = project_corner(&forward, &right, &up, camera->focal_length, half_horiz, half_vert);
	frame->se = project_corner(&forward, &right, &down, camera->focal_length, half_horiz, half_vert);
	frame->sw = project_corner(&forward, &left, &down, camera->focal_length, half_horiz, half_vert);
}


/* Prepare a frame of camera projection, initializing res_frame. */
void ray_camera_frame_prepare(ray_camera_t *camera, ray_camera_frame_t *res_frame)
{
	res_frame->camera = camera;

	project_corners(camera, res_frame);

	res_frame->x_delta = 1.0f / (float)camera->width;
	res_frame->y_delta = 1.0f / (float)camera->height;
}


/* Begin a frame's fragment, initializing frame and ray. */
void ray_camera_fragment_begin(ray_camera_frame_t *frame, fb_fragment_t *fb_fragment, ray_ray_t *res_ray, ray_camera_fragment_t *res_fragment)
{
	res_fragment->frame = frame;
	res_fragment->fb_fragment = fb_fragment;
	res_fragment->ray = res_ray;

	res_fragment->x = res_fragment->y = 0;

	res_fragment->x_alpha = frame->x_delta * (float)fb_fragment->x;
	res_fragment->y_alpha = frame->y_delta * (float)fb_fragment->y;

	res_fragment->cur_w = ray_3f_lerp(&frame->nw, &frame->sw, res_fragment->y_alpha);
	res_fragment->cur_e = ray_3f_lerp(&frame->ne, &frame->se, res_fragment->y_alpha);

	res_ray->origin = frame->camera->position;
	res_ray->direction = ray_3f_nlerp(&res_fragment->cur_w, &res_fragment->cur_e, res_fragment->x_alpha);
}
