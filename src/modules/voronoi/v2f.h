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
 * 2D vector operations header
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
 *	v2f_t	foo, *foop;
 *
 *	foo = _v2f_mult(&(v2f_t){1.f,1.f}, &(v2f_t){2.f,2.f});
 *
 *	is equivalent to:
 *
 *	v2f_mult(&foo, &(v2f_t){1.f,1.f}, &(v2f_t){2.f,2.f});
 *
 *	or dynamically allocated:
 *
 *	foop = v2f_mult(NULL, &(v2f_t){1.f,1.f}, &(v2f_t){2.f,2.f});
 *	free(foop);
 *
 *	is equivalent to:
 *
 *	foop = malloc(sizeof(v2f_t));
 *	v2f_mult(foop, &(v2f_t){1.f,1.f}, &(v2f_t){2.f,2.f});
 *	free(foop);
 */

#ifndef _V2F_H
#define _V2F_H


#include <math.h>
#include <stdlib.h>


typedef struct v2f_t {
	float	x, y;
} v2f_t;


static inline v2f_t * _v2f_allocated(v2f_t **ptr)
{
	if (!*ptr)
		*ptr = malloc(sizeof(v2f_t));

	return *ptr;
}


static inline v2f_t _v2f_add(const v2f_t *a, const v2f_t *b)
{
	return (v2f_t){a->x + b->x, a->y + b->y};
}


static inline v2f_t * v2f_add(v2f_t *res, const v2f_t *a, const v2f_t *b)
{
	if (_v2f_allocated(&res))
		*res = _v2f_add(a, b);

	return res;
}


static inline v2f_t _v2f_sub(const v2f_t *a, const v2f_t *b)
{
	return (v2f_t){a->x - b->x, a->y - b->y};
}


static inline v2f_t * v2f_sub(v2f_t *res, const v2f_t *a, const v2f_t *b)
{
	if (_v2f_allocated(&res))
		*res = _v2f_sub(a, b);

	return res;
}


static inline v2f_t _v2f_mult(const v2f_t *a, const v2f_t *b)
{
	return (v2f_t){a->x * b->x, a->y * b->y};
}


static inline v2f_t * v2f_mult(v2f_t *res, const v2f_t *a, const v2f_t *b)
{
	if (_v2f_allocated(&res))
		*res = _v2f_mult(a, b);

	return res;
}


static inline v2f_t _v2f_mult_scalar(const v2f_t *v, float scalar)
{
	return (v2f_t){ v->x * scalar, v->y * scalar };
}


static inline v2f_t * v2f_mult_scalar(v2f_t *res, const v2f_t *v, float scalar)
{
	if (_v2f_allocated(&res))
		*res = _v2f_mult_scalar(v, scalar);

	return res;
}


static inline v2f_t _v2f_div_scalar(const v2f_t *v, float scalar)
{
	return _v2f_mult_scalar(v, 1.f / scalar);
}


static inline v2f_t * v2f_div_scalar(v2f_t *res, const v2f_t *v, float scalar)
{
	if (_v2f_allocated(&res))
		*res = _v2f_div_scalar(v, scalar);

	return res;
}


static inline float v2f_dot(const v2f_t *a, const v2f_t *b)
{
	return a->x * b->x + a->y * b->y;
}


static inline float v2f_length(const v2f_t *v)
{
	return sqrtf(v2f_dot(v, v));
}


static inline float v2f_distance(const v2f_t *a, const v2f_t *b)
{
	return v2f_length(v2f_sub(&(v2f_t){}, a, b));
}


static inline float v2f_distance_sq(const v2f_t *a, const v2f_t *b)
{
	v2f_t	d = _v2f_sub(a, b);

	return v2f_dot(&d, &d);
}


static inline v2f_t _v2f_normalize(const v2f_t *v)
{
	return _v2f_mult_scalar(v, 1.0f / v2f_length(v));
}


static inline v2f_t * v2f_normalize(v2f_t *res, const v2f_t *v)
{
	if (_v2f_allocated(&res))
		*res = _v2f_normalize(v);

	return res;
}


static inline v2f_t _v2f_lerp(const v2f_t *a, const v2f_t *b, float t)
{
	v2f_t	lerp_a, lerp_b;

	lerp_a = _v2f_mult_scalar(a, 1.0f - t);
	lerp_b = _v2f_mult_scalar(b, t);

	return _v2f_add(&lerp_a, &lerp_b);
}


static inline v2f_t * v2f_lerp(v2f_t *res, const v2f_t *a, const v2f_t *b, float t)
{
	if (_v2f_allocated(&res))
		*res = _v2f_lerp(a, b, t);

	return res;
}


static inline v2f_t _v2f_nlerp(const v2f_t *a, const v2f_t *b, float t)
{
	v2f_t	lerp = _v2f_lerp(a, b, t);

	return _v2f_normalize(&lerp);
}


static inline v2f_t * v2f_nlerp(v2f_t *res, const v2f_t *a, const v2f_t *b, float t)
{
	if (_v2f_allocated(&res))
		*res = _v2f_nlerp(a, b, t);

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
static inline v2f_t _v2f_bilerp(const v2f_t *aa, const v2f_t *ab, const v2f_t *ba, const v2f_t *bb, float t_x, float t_y)
{
	v2f_t	xa = _v2f_lerp(aa, ba, t_x);
	v2f_t	xb = _v2f_lerp(ab, bb, t_x);

	return _v2f_lerp(&xa, &xb, t_y);
}


static inline v2f_t * v2f_bilerp(v2f_t *res, const v2f_t *aa, const v2f_t *ab, const v2f_t *ba, const v2f_t *bb, float t_x, float t_y)
{
	if (_v2f_allocated(&res))
		*res = _v2f_bilerp(aa, ab, ba, bb, t_x, t_y);

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
static inline v2f_t _v2f_trilerp(const v2f_t *aaa, const v2f_t *aba, const v2f_t *aab, const v2f_t *abb, const v2f_t *baa, const v2f_t *bba, const v2f_t *bab, const v2f_t *bbb, float t_x, float t_y, float t_z)
{
	v2f_t	xya = _v2f_bilerp(aaa, aba, baa, bba, t_x, t_y);
	v2f_t	xyb = _v2f_bilerp(aab, abb, bab, bbb, t_x, t_y);

	return _v2f_lerp(&xya, &xyb, t_z);
}


static inline v2f_t * v2f_trilerp(v2f_t *res, const v2f_t *aaa, const v2f_t *aba, const v2f_t *aab, const v2f_t *abb, const v2f_t *baa, const v2f_t *bba, const v2f_t *bab, const v2f_t *bbb, float t_x, float t_y, float t_z)
{
	if (_v2f_allocated(&res))
		*res = _v2f_trilerp(aaa, aba, aab, abb, baa, bba, bab, bbb, t_x, t_y, t_z);

	return res;
}


static inline v2f_t _v2f_rand(const v2f_t *min, const v2f_t *max)
{
	return (v2f_t){
		.x = min->x + (float)rand() * (1.f/(float)RAND_MAX) * (max->x - min->x),
		.y = min->y + (float)rand() * (1.f/(float)RAND_MAX) * (max->y - min->y),
	};
}


static inline v2f_t * v2f_rand(v2f_t *res, const v2f_t *min, const v2f_t *max)
{
	if (_v2f_allocated(&res))
		*res = _v2f_rand(min, max);

	return res;
}


static inline v2f_t _v2f_ceil(const v2f_t *v)
{
	return (v2f_t){
		.x = ceilf(v->x),
		.y = ceilf(v->y),
	};
}


static inline v2f_t * v2f_ceil(v2f_t *res, const v2f_t *v)
{
	if (_v2f_allocated(&res))
		*res = _v2f_ceil(v);

	return res;
}


static inline v2f_t _v2f_floor(const v2f_t *v)
{
	return (v2f_t){
		.x = floorf(v->x),
		.y = floorf(v->y),
	};
}


static inline v2f_t * v2f_floor(v2f_t *res, const v2f_t *v)
{
	if (_v2f_allocated(&res))
		*res = _v2f_floor(v);

	return res;
}


static inline v2f_t _v2f_clamp(const v2f_t *v, const v2f_t *min, const v2f_t *max)
{
	return (v2f_t){
		.x = v->x < min->x ? min->x : v->x > max->x ? max->x : v->x,
		.y = v->y < min->y ? min->y : v->y > max->y ? max->y : v->y,
	};
}


static inline v2f_t * v2f_clamp(v2f_t *res, const v2f_t *v, const v2f_t *min, const v2f_t *max)
{
	if (_v2f_allocated(&res))
		*res = _v2f_clamp(v, min, max);

	return res;
}

#endif
