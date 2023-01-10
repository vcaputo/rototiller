#ifndef _TIL_TAP_H
#define _TIL_TAP_H

#include <stdint.h>

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
	void		*ptr;		/* points at the caller-provided tap-managed indirection pointer */
	size_t		n_elems;	/* when > 1, *ptr is an array of n_elems elements.  Otherwise individual variable. */
	void		*elems;		/* points at the first element of type type, may or may not be an array of them */
	const char	*name;
} til_tap_t;

/* just some forward declared higher-order vector and matrix types for the wrappers */
typedef struct v2f_t v2f_t;
typedef struct v3f_t v3f_t;
typedef struct v4f_t v4f_t;
typedef struct m4f_t m4f_t;

/* This is the bare tap initializer but use the type-checked wrappers below and add one if one's missing */
static inline void til_tap_init(til_tap_t *tap, til_tap_type_t type, void *ptr, size_t n_elems, void *elems, const char *name)
{
	assert(tap);
	assert(type < TIL_TAP_TYPE_MAX);
	assert(ptr);
	assert(n_elems);
	assert(elems);
	assert(name);

	tap->type = type;
	tap->ptr = ptr;
	tap->n_elems = n_elems;
	tap->elems = elems;
	tap->name = name;

	*((void **)tap->ptr) = elems;
}

/* typed wrappers, just supply n_elems=1 for individual variables - note n_elems is just a defensive
 * programming sanity check to catch callers mismatching array sizes
 */
static inline void til_tap_init_i8(til_tap_t *tap, int8_t **ptr, size_t n_elems, int8_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_I8, ptr, n_elems, elems, name);
}

static inline void til_tap_init_i16(til_tap_t *tap, int16_t **ptr, size_t n_elems, int16_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_I16, ptr, n_elems, elems, name);
}

static inline void til_tap_init_i32(til_tap_t *tap, int32_t **ptr, size_t n_elems, int32_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_I32, ptr, n_elems, elems, name);
}

static inline void til_tap_init_i64(til_tap_t *tap, int64_t **ptr, size_t n_elems, int64_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_I64, ptr, n_elems, elems, name);
}

static inline void til_tap_init_u8(til_tap_t *tap, uint8_t **ptr, size_t n_elems, uint8_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_U8, ptr, n_elems, elems, name);
}

static inline void til_tap_init_u16(til_tap_t *tap, uint16_t **ptr, size_t n_elems, uint16_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_U16, ptr, n_elems, elems, name);
}

static inline void til_tap_init_u32(til_tap_t *tap, uint32_t **ptr, size_t n_elems, uint32_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_U32, ptr, n_elems, elems, name);
}

static inline void til_tap_init_u64(til_tap_t *tap, uint64_t **ptr, size_t n_elems, uint64_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_U64, ptr, n_elems, elems, name);
}

static inline void til_tap_init_float(til_tap_t *tap, float **ptr, size_t n_elems, float *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_FLOAT, ptr, n_elems, elems, name);
}

static inline void til_tap_init_double(til_tap_t *tap, double **ptr, size_t n_elems, double *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_DOUBLE, ptr, n_elems, elems, name);
}

static inline void til_tap_init_v2f(til_tap_t *tap, v2f_t **ptr, size_t n_elems, v2f_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_V2F, ptr, n_elems, elems, name);
}

static inline void til_tap_init_v3f(til_tap_t *tap, v3f_t **ptr, size_t n_elems, v3f_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_V3F, ptr, n_elems, elems, name);
}

static inline void til_tap_init_v4f(til_tap_t *tap, v4f_t **ptr, size_t n_elems, v4f_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_V4F, ptr, n_elems, elems, name);
}

static inline void til_tap_init_m4f(til_tap_t *tap, m4f_t **ptr, size_t n_elems, m4f_t *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_M4F, ptr, n_elems, elems, name);
}

static inline void til_tap_init_voidp(til_tap_t *tap, void **ptr, size_t n_elems, void *elems, const char *name)
{
	return til_tap_init(tap, TIL_TAP_TYPE_VOIDP, ptr, n_elems, elems, name);
}

#endif
