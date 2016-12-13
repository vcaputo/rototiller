#ifndef _RAY_3F_H
#define _RAY_3F_H

#include <math.h>

typedef struct ray_3f_t {
	float	x, y, z;
} ray_3f_t;


/* return the result of (a + b) */
static inline ray_3f_t ray_3f_add(ray_3f_t *a, ray_3f_t *b)
{
	ray_3f_t	res = {
				.x = a->x + b->x,
				.y = a->y + b->y,
				.z = a->z + b->z,
			};

	return res;
}


/* return the result of (a - b) */
static inline ray_3f_t ray_3f_sub(ray_3f_t *a, ray_3f_t *b)
{
	ray_3f_t	res = {
				.x = a->x - b->x,
				.y = a->y - b->y,
				.z = a->z - b->z,
			};

	return res;
}


/* return the result of (-v) */
static inline ray_3f_t ray_3f_negate(ray_3f_t *v)
{
	ray_3f_t	res = {
				.x = -v->x,
				.y = -v->y,
				.z = -v->z,
			};

	return res;
}


/* return the result of (a * b) */
static inline ray_3f_t ray_3f_mult(ray_3f_t *a, ray_3f_t *b)
{
	ray_3f_t	res = {
				.x = a->x * b->x,
				.y = a->y * b->y,
				.z = a->z * b->z,
			};

	return res;
}


/* return the result of (v * scalar) */
static inline ray_3f_t ray_3f_mult_scalar(ray_3f_t *v, float scalar)
{
	ray_3f_t	res = {
				.x = v->x * scalar,
				.y = v->y * scalar,
				.z = v->z * scalar,
			};

	return res;
}


/* return the result of (uv / scalar) */
static inline ray_3f_t ray_3f_div_scalar(ray_3f_t *v, float scalar)
{
	ray_3f_t	res = {
				.x = v->x / scalar,
				.y = v->y / scalar,
				.z = v->z / scalar,
			};

	return res;
}


/* return the result of (a . b) */
static inline float ray_3f_dot(ray_3f_t *a, ray_3f_t *b)
{
	return a->x * b->x + a->y * b->y + a->z * b->z;
}


/* return the length of the supplied vector */
static inline float ray_3f_length(ray_3f_t *v)
{
	return sqrtf(ray_3f_dot(v, v));
}


/* return the normalized form of the supplied vector */
static inline ray_3f_t ray_3f_normalize(ray_3f_t *v)
{
	ray_3f_t	nv;
	float		f;

	f = 1.0f / ray_3f_length(v);

	nv.x = f * v->x;
	nv.y = f * v->y;
	nv.z = f * v->z;

	return nv;
}


/* return the distance between two arbitrary points */
static inline float ray_3f_distance(ray_3f_t *a, ray_3f_t *b)
{
	return sqrtf(powf(a->x - b->x, 2) + powf(a->y - b->y, 2) + powf(a->z - b->z, 2));
}


/* return the cross product of two unit vectors */
static inline ray_3f_t ray_3f_cross(ray_3f_t *a, ray_3f_t *b)
{
	ray_3f_t	product;

	product.x = a->y * b->z - a->z * b->y;
	product.y = a->z * b->x - a->x * b->z;
	product.z = a->x * b->y - a->y * b->x;

	return product;
}


/* return the linearly interpolated vector between the two vectors at point alpha (0-1.0) */
static inline ray_3f_t ray_3f_lerp(ray_3f_t *a, ray_3f_t *b, float alpha)
{
	ray_3f_t	lerp_a, lerp_b;

	lerp_a = ray_3f_mult_scalar(a, 1.0f - alpha);
	lerp_b = ray_3f_mult_scalar(b, alpha);

	return ray_3f_add(&lerp_a, &lerp_b);
}


/* return the normalized linearly interpolated vector between the two vectors at point alpha (0-1.0) */
static inline ray_3f_t ray_3f_nlerp(ray_3f_t *a, ray_3f_t *b, float alpha)
{
	ray_3f_t	lerp;

	lerp = ray_3f_lerp(a, b, alpha);

	return ray_3f_normalize(&lerp);
}

#endif
