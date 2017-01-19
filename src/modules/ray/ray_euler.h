#ifndef _RAY_EULER_H
#define _RAY_EULER_H

#include <math.h>

#include "ray_3f.h"


/* euler angles are convenient for describing orientation */
typedef struct ray_euler_t {
	float	pitch;	/* pitch in radiasn */
	float	yaw;	/* yaw in radians */
	float	roll;	/* roll in radians */
} ray_euler_t;


/* convenience macro for converting degrees to radians */
#define RAY_EULER_DEGREES(_deg) \
	(_deg * (2 * M_PI / 360.0f))


/* produce basis vectors from euler angles */
static inline void ray_euler_basis(ray_euler_t *e, ray_3f_t *forward, ray_3f_t *up, ray_3f_t *left)
{
	float	cos_yaw = cosf(e->yaw);
	float	sin_yaw = sinf(e->yaw);
	float	cos_roll = cosf(e->roll);
	float	sin_roll = sinf(e->roll);
	float	cos_pitch = cosf(e->pitch);
	float	sin_pitch = sinf(e->pitch);

	forward->x = sin_yaw;
	forward->y = -sin_pitch * cos_yaw;
	forward->z = cos_pitch * cos_yaw;

	up->x = -cos_yaw * sin_roll;
	up->y = -sin_pitch * sin_yaw * sin_roll + cos_pitch * cos_roll;
	up->z = cos_pitch * sin_yaw * sin_roll + sin_pitch * cos_roll;

	left->x = cos_yaw * cos_roll;
	left->y = sin_pitch * sin_yaw * cos_roll + cos_pitch * sin_roll;
	left->z = -cos_pitch * sin_yaw * cos_roll + sin_pitch * sin_roll;
}

#endif
