#include <assert.h>
#include <math.h>

#include "ray_3f.h"
#include "ray_euler.h"

/* produce orthonormal basis vectors from euler angles, rotated in the specified order */
void ray_euler_basis(ray_euler_t *e, ray_3f_t *forward, ray_3f_t *up, ray_3f_t *left)
{
	float	cos_yaw = cosf(e->yaw);
	float	sin_yaw = sinf(e->yaw);
	float	cos_roll = cosf(e->roll);
	float	sin_roll = sinf(e->roll);
	float	cos_pitch = cosf(e->pitch);
	float	sin_pitch = sinf(e->pitch);

	/* Rotation matrices from http://www.songho.ca/opengl/gl_anglestoaxes.html */
	switch (e->order) {
	case RAY_EULER_ORDER_PYR:
		/* pitch, yaw, roll */
		up->x = -cos_yaw * sin_roll;
		up->y = -sin_pitch * sin_yaw * sin_roll + cos_pitch * cos_roll;
		up->z = cos_pitch * sin_yaw * sin_roll + sin_pitch * cos_roll;

		forward->x = sin_yaw;
		forward->y = -sin_pitch * cos_yaw;
		forward->z = cos_pitch * cos_yaw;
		break;

	case RAY_EULER_ORDER_YRP:
		/* yaw, roll, pitch */
		up->x = -cos_yaw * sin_roll * cos_pitch + sin_yaw * sin_pitch;
		up->y = cos_roll * cos_pitch;
		up->z = sin_yaw * sin_roll * cos_pitch + cos_yaw * sin_pitch;

		forward->x = cos_yaw * sin_roll * sin_pitch + sin_yaw * cos_pitch;
		forward->y = -cos_roll * sin_pitch;
		forward->z = -sin_yaw * sin_roll * sin_pitch + cos_yaw * cos_pitch;
		break;

	case RAY_EULER_ORDER_RPY:
		/* roll, pitch, yaw */
		up->x = -sin_roll * cos_pitch;
		up->y = cos_roll * cos_pitch;
		up->z = sin_pitch;

		forward->x = cos_roll * sin_yaw + sin_roll * sin_pitch * cos_yaw;
		forward->y = sin_roll * sin_yaw - cos_roll * sin_pitch * cos_yaw;
		forward->z = cos_pitch * cos_yaw;
		break;

	case RAY_EULER_ORDER_PRY:
		/* pitch, roll, yaw */
		up->x = -sin_roll;
		up->y = cos_pitch * cos_roll;
		up->z = sin_pitch * cos_roll;

		forward->x = cos_roll * sin_yaw;
		forward->y = cos_pitch * sin_roll * sin_yaw - sin_pitch * cos_yaw;
		forward->z = sin_pitch * sin_roll * sin_yaw + cos_pitch * cos_yaw;
		break;

	case RAY_EULER_ORDER_RYP:
		/* roll, yaw, pitch */
		up->x = -sin_roll * cos_pitch + cos_roll * sin_yaw * sin_pitch;
		up->y = cos_roll * cos_pitch + sin_roll * sin_yaw * sin_pitch;
		up->z = cos_yaw * sin_pitch;

		forward->x = sin_roll * sin_pitch + cos_roll * sin_yaw * cos_pitch;
		forward->y = -cos_roll * sin_pitch + sin_roll * sin_yaw * cos_pitch;
		forward->z = cos_yaw * cos_pitch;
		break;

	case RAY_EULER_ORDER_YPR:
		/* yaw, pitch, roll */
		up->x = -cos_yaw * sin_roll + sin_yaw * sin_pitch * cos_roll;
		up->y = cos_pitch * cos_roll;
		up->z = sin_yaw * sin_roll + cos_yaw * sin_pitch * cos_roll;

		forward->x = sin_yaw * cos_pitch;
		forward->y = -sin_pitch;
		forward->z = cos_yaw * cos_pitch;
		break;

	default:
		assert(0);
	}

	*left = ray_3f_cross(up, forward);
}
