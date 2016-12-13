#ifndef _V3F_H
#define _V3F_H

#include <math.h>

typedef struct v3f_t {
	float	x, y, z;
} v3f_t;

#define v3f_set(_v3f, _x, _y, _z)	\
	(_v3f)->x = _x;			\
	(_v3f)->y = _y;			\
	(_v3f)->z = _z;

#define v3f_init(_x, _y, _z)		\
	{				\
		.x = _x,		\
		.y = _y,		\
		.z = _z,		\
	}

/* return if a and b are equal */
static inline int v3f_equal(v3f_t *a, v3f_t *b)
{
	return (a->x == b->x && a->y == b->y && a->z == b->z);
}


/* return the result of (a + b) */
static inline v3f_t v3f_add(v3f_t *a, v3f_t *b)
{
	v3f_t	res = v3f_init(a->x + b->x, a->y + b->y, a->z + b->z);

	return res;
}


/* return the result of (a - b) */
static inline v3f_t v3f_sub(v3f_t *a, v3f_t *b)
{
	v3f_t	res = v3f_init(a->x - b->x, a->y - b->y, a->z - b->z);

	return res;
}


/* return the result of (-v) */
static inline v3f_t v3f_negate(v3f_t *v)
{
	v3f_t	res = v3f_init(-v->x, -v->y, -v->z);

	return res;
}


/* return the result of (a * b) */
static inline v3f_t v3f_mult(v3f_t *a, v3f_t *b)
{
	v3f_t	res = v3f_init(a->x * b->x, a->y * b->y, a->z * b->z);

	return res;
}


/* return the result of (v * scalar) */
static inline v3f_t v3f_mult_scalar(v3f_t *v, float scalar)
{
	v3f_t	res = v3f_init( v->x * scalar, v->y * scalar, v->z * scalar);

	return res;
}


/* return the result of (uv / scalar) */
static inline v3f_t v3f_div_scalar(v3f_t *v, float scalar)
{
	v3f_t	res = v3f_init(v->x / scalar, v->y / scalar, v->z / scalar);

	return res;
}


/* return the result of (a . b) */
static inline float v3f_dot(v3f_t *a, v3f_t *b)
{
	return a->x * b->x + a->y * b->y + a->z * b->z;
}


/* return the length of the supplied vector */
static inline float v3f_length(v3f_t *v)
{
	return sqrtf(v3f_dot(v, v));
}


/* return the normalized form of the supplied vector */
static inline v3f_t v3f_normalize(v3f_t *v)
{
	v3f_t	nv;
	float	f;

	f = 1.0f / v3f_length(v);

	v3f_set(&nv, f * v->x, f * v->y, f * v->z);

	return nv;
}


/* return the distance squared between two arbitrary points */
static inline float v3f_distance_sq(v3f_t *a, v3f_t *b)
{
	return powf(a->x - b->x, 2) + powf(a->y - b->y, 2) + powf(a->z - b->z, 2);
}


/* return the distance between two arbitrary points */
/* (consider using v3f_distance_sq() instead if possible, sqrtf() is slow) */
static inline float v3f_distance(v3f_t *a, v3f_t *b)
{
	return sqrtf(v3f_distance_sq(a, b));
}


/* return the cross product of two unit vectors */
static inline v3f_t v3f_cross(v3f_t *a, v3f_t *b)
{
	v3f_t	product = v3f_init(a->y * b->z - a->z * b->y, a->z * b->x - a->x * b->z, a->x * b->y - a->y * b->x);

	return product;
}


/* return the linearly interpolated vector between the two vectors at point alpha (0-1.0) */
static inline v3f_t v3f_lerp(v3f_t *a, v3f_t *b, float alpha)
{
	v3f_t	lerp_a, lerp_b;

	lerp_a = v3f_mult_scalar(a, 1.0f - alpha);
	lerp_b = v3f_mult_scalar(b, alpha);

	return v3f_add(&lerp_a, &lerp_b);
}


/* return the normalized linearly interpolated vector between the two vectors at point alpha (0-1.0) */
static inline v3f_t v3f_nlerp(v3f_t *a, v3f_t *b, float alpha)
{
	v3f_t	lerp;

	lerp = v3f_lerp(a, b, alpha);

	return v3f_normalize(&lerp);
}

#endif
