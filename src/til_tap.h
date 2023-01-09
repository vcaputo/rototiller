#ifndef _TIL_TAP_H
#define _TIL_TAP_H

#include <stdint.h>

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

typedef struct til_tap_t til_tap_t;

til_tap_t * til_tap_new(til_tap_type_t type, void *ptr, const char *name, size_t n_elems, void *elems);
til_tap_t * til_tap_free(til_tap_t *tap);

/* just some forward declared higher-order vector and matrix types for the wrappers */
typedef struct v2f_t v2f_t;
typedef struct v3f_t v3f_t;
typedef struct v4f_t v4f_t;
typedef struct m4f_t m4f_t;

/* typed wrappers, just supply n_elems=1 for individual variables - note n_elems is just a defensive
 * programming sanity check to catch callers mismatching array sizes
 */
static inline til_tap_t * til_tap_new_i8(int8_t **ptr, const char *name, size_t n_elems, int8_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_I8, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_i16(int16_t **ptr, const char *name, size_t n_elems, int16_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_I16, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_i32(int32_t **ptr, const char *name, size_t n_elems, int32_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_I32, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_i64(int64_t **ptr, const char *name, size_t n_elems, int64_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_I64, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_u8(uint8_t **ptr, const char *name, size_t n_elems, uint8_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_U8, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_u16(uint16_t **ptr, const char *name, size_t n_elems, uint16_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_U16, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_u32(uint32_t **ptr, const char *name, size_t n_elems, uint32_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_U32, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_u64(uint64_t **ptr, const char *name, size_t n_elems, uint64_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_U64, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_float(float **ptr, const char *name, size_t n_elems, float *elems)
{
	return til_tap_new(TIL_TAP_TYPE_FLOAT, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_double(double **ptr, const char *name, size_t n_elems, double *elems)
{
	return til_tap_new(TIL_TAP_TYPE_DOUBLE, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_v2f(v2f_t **ptr, const char *name, size_t n_elems, v2f_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_V2F, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_v3f(v3f_t **ptr, const char *name, size_t n_elems, v3f_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_V3F, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_v4f(v4f_t **ptr, const char *name, size_t n_elems, v4f_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_V4F, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_m4f(m4f_t **ptr, const char *name, size_t n_elems, m4f_t *elems)
{
	return til_tap_new(TIL_TAP_TYPE_M4F, ptr, name, n_elems, elems);
}

static inline til_tap_t * til_tap_new_voidp(void **ptr, const char *name, size_t n_elems, void *elems)
{
	return til_tap_new(TIL_TAP_TYPE_VOIDP, ptr, name, n_elems, elems);
}

#endif
