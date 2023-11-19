#ifndef _V3F_H
#define _V3F_H

#include <math.h>
#include <stdlib.h>

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
static inline int v3f_equal(const v3f_t *a, const v3f_t *b)
{
	return (a->x == b->x && a->y == b->y && a->z == b->z);
}


/* return the result of (a + b) */
static inline v3f_t v3f_add(const v3f_t *a, const v3f_t *b)
{
	v3f_t	res = v3f_init(a->x + b->x, a->y + b->y, a->z + b->z);

	return res;
}


/* return the result of (a - b) */
static inline v3f_t v3f_sub(const v3f_t *a, const v3f_t *b)
{
	v3f_t	res = v3f_init(a->x - b->x, a->y - b->y, a->z - b->z);

	return res;
}


/* return the result of (-v) */
static inline v3f_t v3f_negate(const v3f_t *v)
{
	v3f_t	res = v3f_init(-v->x, -v->y, -v->z);

	return res;
}


/* return the result of (a * b) */
static inline v3f_t v3f_mult(const v3f_t *a, const v3f_t *b)
{
	v3f_t	res = v3f_init(a->x * b->x, a->y * b->y, a->z * b->z);

	return res;
}


/* return the result of (v * scalar) */
static inline v3f_t v3f_mult_scalar(const v3f_t *v, float scalar)
{
	v3f_t	res = v3f_init( v->x * scalar, v->y * scalar, v->z * scalar);

	return res;
}


/* return the result of (uv / scalar) */
static inline v3f_t v3f_div_scalar(const v3f_t *v, float scalar)
{
	v3f_t	res = v3f_init(v->x / scalar, v->y / scalar, v->z / scalar);

	return res;
}


/* return the result of (a . b) */
static inline float v3f_dot(const v3f_t *a, const v3f_t *b)
{
	return a->x * b->x + a->y * b->y + a->z * b->z;
}


/* return the length of the supplied vector */
static inline float v3f_length(const v3f_t *v)
{
	return sqrtf(v3f_dot(v, v));
}


/* return the normalized form of the supplied vector */
static inline v3f_t v3f_normalize(const v3f_t *v)
{
	v3f_t	nv;
	float	f;

	f = 1.0f / v3f_length(v);

	v3f_set(&nv, f * v->x, f * v->y, f * v->z);

	return nv;
}


/* return the distance squared between two arbitrary points */
static inline float v3f_distance_sq(const v3f_t *a, const v3f_t *b)
{
	return powf(a->x - b->x, 2) + powf(a->y - b->y, 2) + powf(a->z - b->z, 2);
}


/* return the distance between two arbitrary points */
/* (consider using v3f_distance_sq() instead if possible, sqrtf() is slow) */
static inline float v3f_distance(const v3f_t *a, const v3f_t *b)
{
	return sqrtf(v3f_distance_sq(a, b));
}


/* return the cross product of two unit vectors */
static inline v3f_t v3f_cross(const v3f_t *a, const v3f_t *b)
{
	v3f_t	product = v3f_init(a->y * b->z - a->z * b->y, a->z * b->x - a->x * b->z, a->x * b->y - a->y * b->x);

	return product;
}


/* return the linearly interpolated vector between the two vectors at point alpha (0-1.0) */
static inline v3f_t v3f_lerp(const v3f_t *a, const v3f_t *b, float alpha)
{
	v3f_t	lerp_a, lerp_b;

	lerp_a = v3f_mult_scalar(a, 1.0f - alpha);
	lerp_b = v3f_mult_scalar(b, alpha);

	return v3f_add(&lerp_a, &lerp_b);
}


/* return the normalized linearly interpolated vector between the two vectors at point alpha (0-1.0) */
static inline v3f_t v3f_nlerp(const v3f_t *a, const v3f_t *b, float alpha)
{
	v3f_t	lerp;

	lerp = v3f_lerp(a, b, alpha);

	return v3f_normalize(&lerp);
}


/* return the bilinearly interpolated value */
/* tx:0---------1
 *   1a---------b
 *   ||         |
 *   ||         |
 *   ||         |
 *   0c---------d
 *   ^
 *   t
 *   y
 */
static inline v3f_t v3f_bilerp(const v3f_t *a, const v3f_t *b, const v3f_t *c, const v3f_t *d, float tx, float ty)
{
	v3f_t	x1, x2;

	x1 = v3f_lerp(a, b, tx);
	x2 = v3f_lerp(c, d, tx);

	return v3f_lerp(&x2, &x1, ty);
}


/* return the trilinearly interpolated value */
/*
 *      e---------f
 *     /|        /|
 *    a---------b |
 *    | |       | |
 *    | g-------|-h
 *    |/        |/
 *    c---------d
 */
static inline v3f_t v3f_trilerp(const v3f_t *a, const v3f_t *b, const v3f_t *c, const v3f_t *d, const v3f_t *e, const v3f_t *f, const v3f_t *g, const v3f_t *h, const v3f_t *t)
{
	v3f_t	abcd, efgh;

	abcd = v3f_bilerp(a, b, c, d, t->x, t->y);
	efgh = v3f_bilerp(e, f, g, h, t->x, t->y);

	return v3f_lerp(&abcd, &efgh, t->z);
}


static inline v3f_t v3f_ceil(const v3f_t *v)
{
	v3f_t	res = v3f_init(ceilf(v->x), ceilf(v->y), ceilf(v->z));

	return res;
}


static inline v3f_t v3f_floor(const v3f_t *v)
{
	v3f_t	res = v3f_init(floorf(v->x), floorf(v->y), floorf(v->z));

	return res;
}


static inline v3f_t v3f_rand(unsigned *seed, float min, float max)
{
	v3f_t	res = v3f_init( (min + ((float)rand_r(seed) * (1.0f/(float)RAND_MAX)) * (max - min)),
				(min + ((float)rand_r(seed) * (1.0f/(float)RAND_MAX)) * (max - min)),
				(min + ((float)rand_r(seed) * (1.0f/(float)RAND_MAX)) * (max - min)));

	return res;
}


static inline v3f_t v3f_clamp(const v3f_t min, const v3f_t max, const v3f_t *v)
{
	v3f_t	res;

	if (v->x < min.x)
		res.x = min.x;
	else if(v->x > max.x)
		res.x = max.x;
	else
		res.x = v->x;

	if (v->y < min.y)
		res.y = min.y;
	else if(v->y > max.y)
		res.y = max.y;
	else
		res.y = v->y;

	if (v->z < min.z)
		res.z = min.z;
	else if(v->z > max.z)
		res.z = max.z;
	else
		res.z = v->z;

	return res;
}


static inline v3f_t v3f_clamp_scalar(float min, float max, const v3f_t *v)
{
	v3f_t	res;

	if (v->x < min)
		res.x = min;
	else if(v->x > max)
		res.x = max;
	else
		res.x = v->x;

	if (v->y < min)
		res.y = min;
	else if(v->y > max)
		res.y = max;
	else
		res.y = v->y;

	if (v->z < min)
		res.z = min;
	else if(v->z > max)
		res.z = max;
	else
		res.z = v->z;

	return res;
}
#endif
