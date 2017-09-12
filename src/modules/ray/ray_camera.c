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
	float		half_horiz = (float)camera->width / 2.0f;
	float		half_vert = (float)camera->height / 2.0f;

	ray_euler_basis(&camera->orientation, &forward, &up, &left);
	right = ray_3f_negate(&left);
	down = ray_3f_negate(&up);

	frame->nw = project_corner(&forward, &left, &up, camera->focal_length, half_horiz,  half_vert);
	frame->ne = project_corner(&forward, &right, &up, camera->focal_length, half_horiz,  half_vert);
	frame->se = project_corner(&forward, &right, &down, camera->focal_length, half_horiz,  half_vert);
	frame->sw = project_corner(&forward, &left, &down, camera->focal_length, half_horiz,  half_vert);
}


/* Begin a frame for the fragment of camera projection, initializing frame and ray. */
void ray_camera_frame_begin(ray_camera_t *camera, fb_fragment_t *fragment, ray_ray_t *ray, ray_camera_frame_t *frame)
{
	/* References are kept to the camera, fragment, and ray to be traced.
	 * The ray is maintained as we step through the frame, that is the
	 * purpose of this api.
	 *
	 * Since the ray direction should be a normalized vector, the obvious
	 * implementation is a bit costly.  The camera frame api hides this
	 * detail so we can explore interpolation techniques to potentially
	 * lessen the per-pixel cost.
	 */
	frame->camera = camera;
	frame->fragment = fragment;
	frame->ray = ray;

	frame->x = frame->y = 0;

	/* From camera->orientation and camera->focal_length compute the vectors
	 * through the viewport's corners, and place these normalized vectors
	 * in frame->(nw,ne,sw,se).
	 *
	 * These can than be interpolated between to produce the ray vectors
	 * throughout the frame's fragment.  The efficient option of linear
	 * interpolation will not maintain the unit vector length, so to
	 * produce normalized interpolated directions will require the costly
	 * normalize function.
	 *
	 * I'm hoping a simple length correction table can be used to fixup the
	 * linearly interpolated vectors to make them unit vectors with just
	 * scalar multiplication instead of the sqrt of normalize.
	 */
	project_corners(camera, frame);

	frame->x_delta = 1.0f / (float)camera->width;
	frame->y_delta = 1.0f / (float)camera->height;
	frame->x_alpha = frame->x_delta * (float)fragment->x;
	frame->y_alpha = frame->y_delta * (float)fragment->y;

	frame->cur_w = ray_3f_lerp(&frame->nw, &frame->sw, frame->y_alpha);
	frame->cur_e = ray_3f_lerp(&frame->ne, &frame->se, frame->y_alpha);

	ray->origin = camera->position;
	ray->direction = ray_3f_nlerp(&frame->cur_w, &frame->cur_e, frame->x_alpha);
}
