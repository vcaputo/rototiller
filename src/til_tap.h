/*
 *  Copyright (C) 2023 - Vito Caputo - <vcaputo@pengaru.com>
 *
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
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

#ifndef _TIL_TAP_H
#define _TIL_TAP_H

#include <stdint.h>
#include <string.h>

#include "til_jenkins.h"

/* A "tap" is a named binding of a local variable+pointer to that variable.
 *
 * Its purpose is to facilitate exposing local variables controlling rendering
 * to potential external influence.
 *
 * The tap itself is not a registry or otherwise discoverable entity by itself.
 * This is strictly just the local glue, Other pieces must index and tie taps
 * into streams or settings stuff for addressing them by name at a path or other
 * means.
 *
 * Note the intended way for taps to work is that the caller will always access their
 * local variables indirectly via the pointers they provided when initializing the taps.
 * There will be a function for managing the tap the caller must call before accessing
 * the variable indirectly as well.  It's that function which will update the indirection
 * pointer to potentially point elsewhere if another tap is driving the variable.
 */

/* These are all the supported tap types, nothing is set in stone this just
 * seemed like the likely stuff to need.  Feel free to add anything as needed.
 */
typedef enum til_tap_type_t {
	TIL_TAP_TYPE_I8,
	TIL_TAP_TYPE_I16,
	TIL_TAP_TYPE_I32,
	TIL_TAP_TYPE_I64,
	TIL_TAP_TYPE_U8,
	TIL_TAP_TYPE_U16,
	TIL_TAP_TYPE_U32,
	TIL_TAP_TYPE_U64,
	TIL_TAP_TYPE_FLOAT,
	TIL_TAP_TYPE_DOUBLE,
	TIL_TAP_TYPE_V2F,	/* 2D vector of floats */
	TIL_TAP_TYPE_V3F,	/* 3D vector of floats */
	TIL_TAP_TYPE_V4F,	/* 4D vector of floats */
	TIL_TAP_TYPE_M4F,	/* 4x4 float matrix */
	TIL_TAP_TYPE_VOIDP,	/* escape hatch for when you're getting exotic and want to bypass type checking */
	TIL_TAP_TYPE_MAX,
} til_tap_type_t;

/* this is deliberately left entirely public so taps can be easily embedded in contexts */
typedef struct til_tap_t {
	til_tap_type_t	type;
	void		**ptr;		/* points at the caller-provided tap-managed indirection pointer */
	size_t		n_elems;	/* when > 1, *ptr is an array of n_elems elements.  Otherwise individual variable. */
	void		*elems;		/* points at the first element of type type, may or may not be an array of them */
	const char	*name;
	uint32_t	name_hash;		/* cached hash of name, set once @ initialization */
} til_tap_t;

/* just some forward declared higher-order vector and matrix types for the wrappers */
typedef struct v2f_t v2f_t;
typedef struct v3f_t v3f_t;
typedef struct v4f_t v4f_t;
typedef struct m4f_t m4f_t;

/* This is the bare tap initializer but use the type-checked wrappers below and add one if one's missing */
static inline til_tap_t til_tap_init(til_tap_type_t type, void *ptr, size_t n_elems, void *elems, const char *name)
{
	assert(type < TIL_TAP_TYPE_MAX);
	assert(ptr);
	assert(n_elems);
	assert(elems);
	assert(name);

	*((void **)ptr) = elems;

	return (til_tap_t){
		.type = type,
		.ptr = ptr,
		.n_elems = n_elems,
		.elems = elems,
		.name = name,
		.name_hash = til_jenkins((uint8_t *)name, strlen(name)),
	};
}

/* typed wrappers, just supply n_elems=1 for individual variables - note n_elems is just a defensive
 * programming sanity check to catch callers mismatching array sizes
 */
static inline til_tap_t til_tap_init_i8(int8_t **ptr, size_t n_elems, int8_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_I8, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_i16(int16_t **ptr, size_t n_elems, int16_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_I16, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_i32(int32_t **ptr, size_t n_elems, int32_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_I32, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_i64(int64_t **ptr, size_t n_elems, int64_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_I64, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_u8(uint8_t **ptr, size_t n_elems, uint8_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_U8, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_u16(uint16_t **ptr, size_t n_elems, uint16_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_U16, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_u32(uint32_t **ptr, size_t n_elems, uint32_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_U32, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_u64(uint64_t **ptr, size_t n_elems, uint64_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_U64, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_float(float **ptr, size_t n_elems, float *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_FLOAT, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_double(double **ptr, size_t n_elems, double *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_DOUBLE, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_v2f(v2f_t **ptr, size_t n_elems, v2f_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_V2F, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_v3f(v3f_t **ptr, size_t n_elems, v3f_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_V3F, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_v4f(v4f_t **ptr, size_t n_elems, v4f_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_V4F, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_m4f(m4f_t **ptr, size_t n_elems, m4f_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_M4F, ptr, n_elems, elems, name);
}

static inline til_tap_t til_tap_init_til_tap_voidp(void **ptr, size_t n_elems, til_tap_t *elems, const char *name)
{
	return til_tap_init(TIL_TAP_TYPE_VOIDP, ptr, n_elems, elems, name);
}

#endif
