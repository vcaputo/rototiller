/*
 *  Copyright (C) 2018-2019 - Vito Caputo - <vcaputo@pengaru.com>
 *
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 3 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * 3D vector operations header
 *
 * Variants prefixed with _ return a result vector struct by value.
 *
 * Variants missing the _ prefix for vector result operations return a pointer
 * and must be either supplied the result memory as the first "res" argument
 * which is then returned after being populated with the result's value, or
 * NULL to allocate space for the result and that pointer is returned after being
 * populated with its value.  When supplying NULL result pointers the functions
 * must allocate memory and thus may return NULL on OOM, so callers should
 * check for NULL returns when supplying NULL for "res".
 *
 * Example:
 *	v3f_t	foo, *foop;
 *
 *	foo = _v3f_mult(&(v3f_t){1.f,1.f,1.f}, &(v3f_t){2.f,2.f,2.f});
 *
 *	is equivalent to:
 *
 *	v3f_mult(&foo, &(v3f_t){1.f,1.f,1.f}, &(v3f_t){2.f,2.f,2.f});
 *
 *	or dynamically allocated:
 *
 *	foop = v3f_mult(NULL, &(v3f_t){1.f,1.f,1.f}, &(v3f_t){2.f,2.f,2.f});
 *	free(foop);
 *
 *	is equivalent to:
 *
 *	foop = malloc(sizeof(v3f_t));
 *	v3f_mult(foop, &(v3f_t){1.f,1.f,1.f}, &(v3f_t){2.f,2.f,2.f});
 *	free(foop);
 */

#ifndef _V3F_H
#define _V3F_H


#include <math.h>
#include <stdlib.h>


typedef struct v3f_t {
	float	x, y, z;
} v3f_t;


static inline v3f_t * _v3f_allocated(v3f_t **ptr)
{
	if (!*ptr)
		*ptr = malloc(sizeof(v3f_t));

	return *ptr;
}


static inline v3f_t _v3f_add(const v3f_t *a, const v3f_t *b)
{
	return (v3f_t){a->x + b->x, a->y + b->y, a->z + b->z};
}


static inline v3f_t * v3f_add(v3f_t *res, const v3f_t *a, const v3f_t *b)
{
	if (_v3f_allocated(&res))
		*res = _v3f_add(a, b);

	return res;
}


static inline v3f_t _v3f_sub(const v3f_t *a, const v3f_t *b)
{
	return (v3f_t){a->x - b->x, a->y - b->y, a->z - b->z};
}


static inline v3f_t * v3f_sub(v3f_t *res, const v3f_t *a, const v3f_t *b)
{
	if (_v3f_allocated(&res))
		*res = _v3f_sub(a, b);

	return res;
}


static inline v3f_t _v3f_mult(const v3f_t *a, const v3f_t *b)
{
	return (v3f_t){a->x * b->x, a->y * b->y, a->z * b->z};
}


static inline v3f_t * v3f_mult(v3f_t *res, const v3f_t *a, const v3f_t *b)
{
	if (_v3f_allocated(&res))
		*res = _v3f_mult(a, b);

	return res;
}


static inline v3f_t _v3f_mult_scalar(const v3f_t *v, float scalar)
{
	return (v3f_t){v->x * scalar, v->y * scalar, v->z * scalar};
}


static inline v3f_t * v3f_mult_scalar(v3f_t *res, const v3f_t *v, float scalar)
{
	if (_v3f_allocated(&res))
		*res = _v3f_mult_scalar(v, scalar);

	return res;
}


static inline v3f_t _v3f_div_scalar(const v3f_t *v, float scalar)
{
	return _v3f_mult_scalar(v, 1.f / scalar);
}


static inline v3f_t * v3f_div_scalar(v3f_t *res, const v3f_t *v, float scalar)
{
	if (_v3f_allocated(&res))
		*res = _v3f_div_scalar(v, scalar);

	return res;
}


static inline float v3f_dot(const v3f_t *a, const v3f_t *b)
{
	return a->x * b->x + a->y * b->y + a->z * b->z;
}


static inline float v3f_length(const v3f_t *v)
{
	return sqrtf(v3f_dot(v, v));
}


static inline float v3f_distance(const v3f_t *a, const v3f_t *b)
{
	return v3f_length(v3f_sub(&(v3f_t){}, a, b));
}


static inline float v3f_distance_sq(const v3f_t *a, const v3f_t *b)
{
	v3f_t	d = _v3f_sub(a, b);

	return v3f_dot(&d, &d);
}


static inline v3f_t _v3f_normalize(const v3f_t *v)
{
	return _v3f_mult_scalar(v, 1.0f / v3f_length(v));
}


static inline v3f_t * v3f_normalize(v3f_t *res, const v3f_t *v)
{
	if (_v3f_allocated(&res))
		*res = _v3f_normalize(v);

	return res;
}


static inline v3f_t _v3f_lerp(const v3f_t *a, const v3f_t *b, float t)
{
	v3f_t	lerp_a, lerp_b;

	lerp_a = _v3f_mult_scalar(a, 1.0f - t);
	lerp_b = _v3f_mult_scalar(b, t);

	return _v3f_add(&lerp_a, &lerp_b);
}


static inline v3f_t * v3f_lerp(v3f_t *res, const v3f_t *a, const v3f_t *b, float t)
{
	if (_v3f_allocated(&res))
		*res = _v3f_lerp(a, b, t);

	return res;
}


static inline v3f_t _v3f_nlerp(const v3f_t *a, const v3f_t *b, float t)
{
	v3f_t	lerp = _v3f_lerp(a, b, t);

	return _v3f_normalize(&lerp);
}


static inline v3f_t * v3f_nlerp(v3f_t *res, const v3f_t *a, const v3f_t *b, float t)
{
	if (_v3f_allocated(&res))
		*res = _v3f_nlerp(a, b, t);

	return res;
}


/*
 *       1 ab-------bb
 *       | |         |
 *       | |         |
 *       | |         |
 *       0 aa-------ba
 *  t_x:   0---------1
 *       ^
 *       t_y
 */
static inline v3f_t _v3f_bilerp(const v3f_t *aa, const v3f_t *ab, const v3f_t *ba, const v3f_t *bb, float t_x, float t_y)
{
	v3f_t	xa = _v3f_lerp(aa, ba, t_x);
	v3f_t	xb = _v3f_lerp(ab, bb, t_x);

	return _v3f_lerp(&xa, &xb, t_y);
}


static inline v3f_t * v3f_bilerp(v3f_t *res, const v3f_t *aa, const v3f_t *ab, const v3f_t *ba, const v3f_t *bb, float t_x, float t_y)
{
	if (_v3f_allocated(&res))
		*res = _v3f_bilerp(aa, ab, ba, bb, t_x, t_y);

	return res;
}


/*
 *     abb-------bbb
 *     /|        /|
 *   aba-------bba|
 *    | |       | |
 *    |aab------|bab
 *    |/        |/
 *   aaa-------baa
 */
static inline v3f_t _v3f_trilerp(const v3f_t *aaa, const v3f_t *aba, const v3f_t *aab, const v3f_t *abb, const v3f_t *baa, const v3f_t *bba, const v3f_t *bab, const v3f_t *bbb, float t_x, float t_y, float t_z)
{
	v3f_t	xya = _v3f_bilerp(aaa, aba, baa, bba, t_x, t_y);
	v3f_t	xyb = _v3f_bilerp(aab, abb, bab, bbb, t_x, t_y);

	return _v3f_lerp(&xya, &xyb, t_z);
}


static inline v3f_t * v3f_trilerp(v3f_t *res, const v3f_t *aaa, const v3f_t *aba, const v3f_t *aab, const v3f_t *abb, const v3f_t *baa, const v3f_t *bba, const v3f_t *bab, const v3f_t *bbb, float t_x, float t_y, float t_z)
{
	if (_v3f_allocated(&res))
		*res = _v3f_trilerp(aaa, aba, aab, abb, baa, bba, bab, bbb, t_x, t_y, t_z);

	return res;
}


static inline v3f_t _v3f_cross(const v3f_t *a, const v3f_t *b)
{
	return (v3f_t){
		.x = a->y * b->z - a->z * b->y,
		.y = a->z * b->x - a->x * b->z,
		.z = a->x * b->y - a->y * b->x,
	};
}


static inline v3f_t * v3f_cross(v3f_t *res, const v3f_t *a, const v3f_t *b)
{
	if (_v3f_allocated(&res))
		*res = _v3f_cross(a, b);

	return res;
}


static inline v3f_t _v3f_rand(unsigned *seedp, const v3f_t *min, const v3f_t *max)
{
	return (v3f_t){
		.x = min->x + (float)rand_r(seedp) * (1.f/(float)RAND_MAX) * (max->x - min->x),
		.y = min->y + (float)rand_r(seedp) * (1.f/(float)RAND_MAX) * (max->y - min->y),
		.z = min->z + (float)rand_r(seedp) * (1.f/(float)RAND_MAX) * (max->z - min->z),
	};
}


static inline v3f_t * v3f_rand(v3f_t *res, unsigned *seedp, const v3f_t *min, const v3f_t *max)
{
	if (_v3f_allocated(&res))
		*res = _v3f_rand(seedp, min, max);

	return res;
}


static inline v3f_t _v3f_ceil(const v3f_t *v)
{
	return (v3f_t){
		.x = ceilf(v->x),
		.y = ceilf(v->y),
		.z = ceilf(v->z),
	};
}


static inline v3f_t * v3f_ceil(v3f_t *res, const v3f_t *v)
{
	if (_v3f_allocated(&res))
		*res = _v3f_ceil(v);

	return res;
}


static inline v3f_t _v3f_floor(const v3f_t *v)
{
	return (v3f_t){
		.x = floorf(v->x),
		.y = floorf(v->y),
		.z = floorf(v->z),
	};
}


static inline v3f_t * v3f_floor(v3f_t *res, const v3f_t *v)
{
	if (_v3f_allocated(&res))
		*res = _v3f_floor(v);

	return res;
}

#endif
