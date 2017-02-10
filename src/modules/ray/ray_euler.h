#ifndef _RAY_EULER_H
#define _RAY_EULER_H

#include <math.h>

#include "ray_3f.h"

/* Desired order to apply euler angle rotations */
typedef enum ray_euler_order_t {
	RAY_EULER_ORDER_PYR,
	RAY_EULER_ORDER_YRP,
	RAY_EULER_ORDER_RPY,
	RAY_EULER_ORDER_PRY,
	RAY_EULER_ORDER_RYP,
	RAY_EULER_ORDER_YPR,
} ray_euler_order_t;

/* euler angles are convenient for describing orientation */
typedef struct ray_euler_t {
	ray_euler_order_t	order;	/* order to apply rotations in */
	float			pitch;	/* pitch in radiasn */
	float			yaw;	/* yaw in radians */
	float			roll;	/* roll in radians */
} ray_euler_t;


/* convenience macro for converting degrees to radians */
#define RAY_EULER_DEGREES(_deg) \
	(_deg * (2 * M_PI / 360.0f))

void ray_euler_basis(ray_euler_t *e, ray_3f_t *forward, ray_3f_t *up, ray_3f_t *left);

#endif
