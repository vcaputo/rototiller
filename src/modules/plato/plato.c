/*
 *  Copyright (C) 2020 - Vito Caputo - <vcaputo@pengaru.com>
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

/* This implements rudimentary 3D drawing of the convex regular polyhedra, AKA
 * Platonic solids, without resorting to conventional tessellated triangle
 * rasterization.
 *
 * Instead the five polyhedra are described by enumerating the vertices of their
 * faces, in a winding order, accompanied by their edge counts and unique vertex
 * counts.  From these two counts, according to Euler's convex polyhedron rule,
 * we can trivially compute the number of faces, (E - V + 2) and from the number
 * of faces to draw derive the number of vertices to apply per face.
 *
 * No fancy texture mapping is performed, at this time only a wireframe is
 * rendered but flat shaded polygons would be fun and relatively easy to
 * implement.
 *
 * It would be interesting to procedurally generate the vertex lists, which
 * should be fairly trivial given the regularity and symmetry.
 *
 * TODO:
 * - hidden surface removal (solid)
 * - filled polygons
 * - shaded polygons
 * - combined/nested rendering of duals:
 *   https://en.wikipedia.org/wiki/Convex_regular_polyhedron#Dual_polyhedra
 */

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_stream.h"
#include "til_tap.h"

#define PLATO_DEFAULT_ORBIT_RATE	.25
#define PLATO_DEFAULT_SPIN_RATE		.75

typedef struct plato_setup_t {
	til_setup_t		til_setup;
	float			orbit_rate;
	float			spin_rate;
} plato_setup_t;

typedef struct plato_context_t {
	til_module_context_t	til_module_context;
	plato_setup_t		setup;

	struct {
		til_tap_t		orbit_rate;
		til_tap_t		spin_rate;
	}			taps;

	float			*orbit_rate;
	float			*spin_rate;

	float			r, rr;
} plato_context_t;

typedef struct v3f_t {
	float			x, y, z;
} v3f_t;

typedef struct polyhedron_t {
	const char		*name;
	unsigned		edge_cnt, vertex_cnt;
	unsigned		n_vertices;		/* size of vertices[] which enumerates all vertices in face order */
	v3f_t			vertices[];
} polyhedron_t;


/* vertex coordinates from:
 * http://paulbourke.net/geometry/platonic/
 * TODO: procedurally generate these all @ runtime
 */

static polyhedron_t	tetrahedron = {
	.name = "tetrahedron",
	.edge_cnt = 6,
	.vertex_cnt = 4,

	.n_vertices = 12,
	.vertices = {
		{ .5f,  .5f,  .5f},
		{-.5f,  .5f, -.5f},
		{ .5f, -.5f, -.5f},

		{-.5f,  .5f, -.5f},
		{-.5f, -.5f,  .5f},
		{ .5f, -.5f, -.5f},

		{ .5f,  .5f,  .5f},
		{ .5f, -.5f, -.5f},
		{-.5f, -.5f,  .5f},

		{ .5f,  .5f,  .5f},
		{-.5f, -.5f,  .5f},
		{-.5f,  .5f, -.5f},
	}
};

static polyhedron_t	hexahedron = {
	.name = "hexahedron",
	.edge_cnt = 12,
	.vertex_cnt = 8,

	.n_vertices = 24,
	.vertices = {
		{-.5f, -.5f, -.5f},
		{ .5f, -.5f, -.5f},
		{ .5f, -.5f,  .5f},
		{-.5f, -.5f,  .5f},

		{-.5f, -.5f, -.5f},
		{-.5f, -.5f,  .5f},
		{-.5f,  .5f,  .5f},
		{-.5f,  .5f, -.5f},

		{-.5f, -.5f,  .5f},
		{ .5f, -.5f,  .5f},
		{ .5f,  .5f,  .5f},
		{-.5f,  .5f,  .5f},

		{-.5f,  .5f, -.5f},
		{-.5f,  .5f,  .5f},
		{ .5f,  .5f,  .5f},
		{ .5f,  .5f, -.5f},

		{ .5f, -.5f, -.5f},
		{ .5f,  .5f, -.5f},
		{ .5f,  .5f,  .5f},
		{ .5f, -.5f,  .5f},

		{-.5f, -.5f, -.5f},
		{-.5f,  .5f, -.5f},
		{ .5f,  .5f, -.5f},
		{ .5f, -.5f, -.5f},
	}
};

#define A	(1.f / (2.f * 1.4142f /*sqrt(2)*/))
#define B	(1.f / 2.f)
static polyhedron_t	octahedron = {
	.name = "octahedron",
	.edge_cnt = 12,
	.vertex_cnt = 6,

	.n_vertices = 24,
	.vertices = {
		{ -A, 0.f,   A},
		{ -A, 0.f,  -A},
		{0.f,   B, 0.f},

		{ -A, 0.f,  -A},
		{  A, 0.f,  -A},
		{0.f,   B, 0.f},

		{  A, 0.f,  -A},
		{  A, 0.f,   A},
		{0.f,   B, 0.f},

		{  A, 0.f,   A},
		{ -A, 0.f,   A},
		{0.f,   B, 0.f},

		{  A, 0.f,  -A},
		{ -A, 0.f,  -A},
		{0.f,  -B, 0.f},

		{ -A, 0.f,  -A},
		{ -A, 0.f,   A},
		{0.f,  -B, 0.f},

		{  A, 0.f,   A},
		{  A, 0.f,  -A},
		{0.f,  -B, 0.f},

		{ -A, 0.f,   A},
		{  A, 0.f,   A},
		{0.f,  -B, 0.f},
	}
};
#undef A
#undef B

#define PHI	((1.f + 2.236f /*sqrt(5)*/) / 2.f)
#define B	((1.f / PHI) / 2.f)
#define C	((2.f - PHI) / 2.f)
static polyhedron_t	dodecahedron = {
	.name = "dodecahedron",
	.edge_cnt = 30,
	.vertex_cnt = 20,

	.n_vertices = 60,
	.vertices = {
		{   C,  0.f,  .5f},
		{  -C,  0.f,  .5f},
		{  -B,    B,    B},
		{ 0.f,  .5f,    C},
		{   B,    B,    B},

		{  -C,  0.f,  .5f},
		{   C,  0.f,  .5f},
		{   B,   -B,    B},
		{ 0.f, -.5f,    C},
		{  -B,   -B,    B},

		{   C,  0.f, -.5f},
		{  -C,  0.f, -.5f},
		{  -B,   -B,   -B},
		{ 0.f, -.5f,   -C},
		{   B,   -B,   -B},

		{  -C,  0.f, -.5f},
		{   C,  0.f, -.5f},
		{   B,    B,   -B},
		{ 0.f,  .5f,   -C},
		{  -B,    B,   -B},

		{ 0.f,  .5f,   -C},
		{ 0.f,  .5f,    C},
		{   B,    B,    B},
		{ .5f,    C,  0.f},
		{   B,    B,   -B},

		{ 0.f,  .5f,    C},
		{ 0.f,  .5f,   -C},
		{  -B,    B,   -B},
		{-.5f,    C,  0.f},
		{  -B,    B,    B},

		{ 0.f, -.5f,   -C},
		{ 0.f, -.5f,    C},
		{  -B,   -B,    B},
		{-.5f,   -C,  0.f},
		{  -B,   -B,   -B},

		{ 0.f, -.5f,    C},
		{ 0.f, -.5f,   -C},
		{   B,   -B,   -B},
		{ .5f,   -C,  0.f},
		{   B,   -B,    B},

		{ .5f,    C,  0.f},
		{ .5f,   -C,  0.f},
		{   B,   -B,    B},
		{   C,  0.f,  .5f},
		{   B,    B,    B},

		{ .5f,   -C,  0.f},
		{ .5f,    C,  0.f},
		{   B,    B,   -B},
		{   C,  0.f, -.5f},
		{   B,   -B,   -B},

		{-.5f,    C,  0.f},
		{-.5f,   -C,  0.f},
		{  -B,   -B,   -B},
		{  -C,  0.f, -.5f},
		{  -B,    B,   -B},

		{-.5f,   -C,  0.f},
		{-.5f,    C,  0.f},
		{  -B,    B,    B},
		{  -C,  0.f,  .5f},
		{  -B,   -B,    B},
	}
};
#undef PHI
#undef B
#undef C

#define PHI	((1.f + 2.236f /*sqrt(5)*/) / 2.f)
#define A	(1.f /2.f)
#define B	(1.f / (2.f * PHI))
static polyhedron_t	icosahedron = {
	.name = "icosahedron",
	.edge_cnt = 30,
	.vertex_cnt = 12,

	.n_vertices = 60,
	.vertices = {
		{0.f,   B,  -A},
		{  B,   A, 0.f},
		{ -B,   A, 0.f},

		{0.f,   B,   A},
		{ -B,   A, 0.f},
		{  B,   A, 0.f},

		{0.f,   B,   A},
		{0.f,  -B,   A},
		{ -A, 0.f,   B},

		{0.f,   B,   A},
		{  A, 0.f,   B},
		{0.f,  -B,   A},

		{0.f,   B,  -A},
		{0.f,  -B,  -A},
		{  A, 0.f,  -B},

		{0.f,   B,  -A},
		{ -A, 0.f,  -B},
		{0.f,  -B,  -A},

		{0.f,  -B,   A},
		{  B,  -A, 0.f},
		{ -B,  -A, 0.f},

		{0.f,  -B,  -A},
		{ -B,  -A, 0.f},
		{  B,  -A, 0.f},

		{ -B,   A, 0.f},
		{ -A, 0.f,   B},
		{ -A, 0.f,  -B},

		{ -B,  -A, 0.f},
		{ -A, 0.f,  -B},
		{ -A, 0.f,   B},

		{  B,   A, 0.f},
		{  A, 0.f,  -B},
		{  A, 0.f,   B},

		{  B,  -A, 0.f},
		{  A, 0.f,   B},
		{  A, 0.f,  -B},

		{0.f,   B,   A},
		{ -A, 0.f,   B},
		{ -B,   A, 0.f},

		{0.f,   B,   A},
		{  B,   A, 0.f},
		{  A, 0.f,   B},

		{0.f,   B,  -A},
		{ -B,   A, 0.f},
		{ -A, 0.f,  -B},

		{0.f,   B,  -A},
		{  A, 0.f,  -B},
		{  B,   A, 0.f},

		{0.f,  -B,  -A},
		{ -A, 0.f,  -B},
		{ -B,  -A, 0.f},

		{0.f,  -B,  -A},
		{  B,  -A, 0.f},
		{  A, 0.f,  -B},

		{0.f,  -B,   A},
		{ -B,  -A, 0.f},
		{ -A, 0.f,   B},

		{0.f,  -B,   A},
		{  A, 0.f,   B},
		{  B,  -A, 0.f},
	}
};
#undef PHI
#undef A
#undef B

static polyhedron_t	*polyhedra[] = {
	&tetrahedron,
	&hexahedron,
	&octahedron,
	&dodecahedron,
	&icosahedron,
};


/* 4x4 matrix type */
typedef struct m4f_t {
	float	m[4][4];
} m4f_t;


/* returns an identity matrix */
static inline m4f_t m4f_identity(void)
{
	return (m4f_t){ .m = {
		{ 1.f, 0.f, 0.f, 0.f },
		{ 0.f, 1.f, 0.f, 0.f },
		{ 0.f, 0.f, 1.f, 0.f },
		{ 0.f, 0.f, 0.f, 1.f },
	}};
}


/* 4x4 X 4x4 matrix multiply */
static inline m4f_t m4f_mult(const m4f_t *a, const m4f_t *b)
{
	m4f_t	r;

	r.m[0][0] = (a->m[0][0] * b->m[0][0]) + (a->m[1][0] * b->m[0][1]) + (a->m[2][0] * b->m[0][2]) + (a->m[3][0] * b->m[0][3]);
	r.m[0][1] = (a->m[0][1] * b->m[0][0]) + (a->m[1][1] * b->m[0][1]) + (a->m[2][1] * b->m[0][2]) + (a->m[3][1] * b->m[0][3]);
	r.m[0][2] = (a->m[0][2] * b->m[0][0]) + (a->m[1][2] * b->m[0][1]) + (a->m[2][2] * b->m[0][2]) + (a->m[3][2] * b->m[0][3]);
	r.m[0][3] = (a->m[0][3] * b->m[0][0]) + (a->m[1][3] * b->m[0][1]) + (a->m[2][3] * b->m[0][2]) + (a->m[3][3] * b->m[0][3]);

	r.m[1][0] = (a->m[0][0] * b->m[1][0]) + (a->m[1][0] * b->m[1][1]) + (a->m[2][0] * b->m[1][2]) + (a->m[3][0] * b->m[1][3]);
	r.m[1][1] = (a->m[0][1] * b->m[1][0]) + (a->m[1][1] * b->m[1][1]) + (a->m[2][1] * b->m[1][2]) + (a->m[3][1] * b->m[1][3]);
	r.m[1][2] = (a->m[0][2] * b->m[1][0]) + (a->m[1][2] * b->m[1][1]) + (a->m[2][2] * b->m[1][2]) + (a->m[3][2] * b->m[1][3]);
	r.m[1][3] = (a->m[0][3] * b->m[1][0]) + (a->m[1][3] * b->m[1][1]) + (a->m[2][3] * b->m[1][2]) + (a->m[3][3] * b->m[1][3]);

	r.m[2][0] = (a->m[0][0] * b->m[2][0]) + (a->m[1][0] * b->m[2][1]) + (a->m[2][0] * b->m[2][2]) + (a->m[3][0] * b->m[2][3]);
	r.m[2][1] = (a->m[0][1] * b->m[2][0]) + (a->m[1][1] * b->m[2][1]) + (a->m[2][1] * b->m[2][2]) + (a->m[3][1] * b->m[2][3]);
	r.m[2][2] = (a->m[0][2] * b->m[2][0]) + (a->m[1][2] * b->m[2][1]) + (a->m[2][2] * b->m[2][2]) + (a->m[3][2] * b->m[2][3]);
	r.m[2][3] = (a->m[0][3] * b->m[2][0]) + (a->m[1][3] * b->m[2][1]) + (a->m[2][3] * b->m[2][2]) + (a->m[3][3] * b->m[2][3]);

	r.m[3][0] = (a->m[0][0] * b->m[3][0]) + (a->m[1][0] * b->m[3][1]) + (a->m[2][0] * b->m[3][2]) + (a->m[3][0] * b->m[3][3]);
	r.m[3][1] = (a->m[0][1] * b->m[3][0]) + (a->m[1][1] * b->m[3][1]) + (a->m[2][1] * b->m[3][2]) + (a->m[3][1] * b->m[3][3]);
	r.m[3][2] = (a->m[0][2] * b->m[3][0]) + (a->m[1][2] * b->m[3][1]) + (a->m[2][2] * b->m[3][2]) + (a->m[3][2] * b->m[3][3]);
	r.m[3][3] = (a->m[0][3] * b->m[3][0]) + (a->m[1][3] * b->m[3][1]) + (a->m[2][3] * b->m[3][2]) + (a->m[3][3] * b->m[3][3]);

	return r;
}


/* 4x4 X 1x3 matrix multiply */
static inline v3f_t m4f_mult_v3f(const m4f_t *a, const v3f_t *b)
{
	v3f_t	v;

	v.x = (a->m[0][0] * b->x) + (a->m[1][0] * b->y) + (a->m[2][0] * b->z) + (a->m[3][0]);
	v.y = (a->m[0][1] * b->x) + (a->m[1][1] * b->y) + (a->m[2][1] * b->z) + (a->m[3][1]);
	v.z = (a->m[0][2] * b->x) + (a->m[1][2] * b->y) + (a->m[2][2] * b->z) + (a->m[3][2]);

	return v;
}


/* adjust the matrix m to translate by v, returning the resulting matrix */
/* if m is NULL the identity vector is assumed */
static inline m4f_t m4f_translate(const m4f_t *m, const v3f_t *v)
{
	m4f_t	identity = m4f_identity();
	m4f_t	translate = m4f_identity();

	if (!m)
		m = &identity;

	translate.m[3][0] = v->x;
	translate.m[3][1] = v->y;
	translate.m[3][2] = v->z;

	return m4f_mult(m, &translate);
}


/* adjust the matrix m to scale by v, returning the resulting matrix */
/* if m is NULL the identity vector is assumed */
static inline m4f_t m4f_scale(const m4f_t *m, const v3f_t *v)
{
	m4f_t	identity = m4f_identity();
	m4f_t	scale = {};

	if (!m)
		m = &identity;

	scale.m[0][0] = v->x;
	scale.m[1][1] = v->y;
	scale.m[2][2] = v->z;
	scale.m[3][3] = 1.f;

	return m4f_mult(m, &scale);
}


/* adjust the matrix m to rotate around the specified axis by radians, returning the resulting matrix */
/* axis is expected to be a unit vector */
/* if m is NULL the identity vector is assumed */
static inline m4f_t m4f_rotate(const m4f_t *m, const v3f_t *axis, float radians)
{
	m4f_t	identity = m4f_identity();
	float	cos_r = cosf(radians);
	float	sin_r = sinf(radians);
	m4f_t	rotate;

	if (!m)
		m = &identity;

	rotate.m[0][0] = cos_r + axis->x * axis->x * (1.f - cos_r);
	rotate.m[0][1] = axis->y * axis->x * (1.f - cos_r) + axis->z * sin_r;
	rotate.m[0][2] = axis->z * axis->x * (1.f - cos_r) - axis->y * sin_r;
	rotate.m[0][3] = 0.f;

	rotate.m[1][0] = axis->x * axis->y * (1.f - cos_r) - axis->z * sin_r;
	rotate.m[1][1] = cos_r + axis->y * axis->y * (1.f - cos_r);
	rotate.m[1][2] = axis->z * axis->y * (1.f - cos_r) + axis->x * sin_r;
	rotate.m[1][3] = 0.f;

	rotate.m[2][0] = axis->x * axis->z * (1.f - cos_r) + axis->y * sin_r;
	rotate.m[2][1] = axis->y * axis->z * (1.f - cos_r) - axis->x * sin_r;
	rotate.m[2][2] = cos_r + axis->z * axis->z * (1.f - cos_r);
	rotate.m[2][3] = 0.f;

	rotate.m[3][0] = 0.f;
	rotate.m[3][1] = 0.f;
	rotate.m[3][2] = 0.f;
	rotate.m[3][3] = 1.f;

	return m4f_mult(m, &rotate);
}


/* this is a simple perpsective projection matrix taken from an opengl tutorial */
static inline m4f_t m4f_frustum(float bot, float top, float left, float right, float nnear, float ffar)
{
	m4f_t	m = {};

	m.m[0][0] = 2 * nnear  / (right - left);

	m.m[1][1] = 2 * nnear / (top - bot);

	m.m[2][0] = (right + left) / (right - left);;
	m.m[2][1] = (top + bot) / (top - bot);
	m.m[2][2] = -(ffar + nnear) / (ffar - nnear);
	m.m[2][3] = -1;

	m.m[3][2] = -2 * ffar * nnear / (ffar - nnear);

	return m;
}


/* convert a color into a packed, 32-bit rgb pixel value (taken from libs/ray/ray_color.h) */
static inline uint32_t color_to_uint32(v3f_t color) {
	uint32_t	pixel;

	if (color.x > 1.0f) color.x = 1.0f;
	if (color.y > 1.0f) color.y = 1.0f;
	if (color.z > 1.0f) color.z = 1.0f;

	if (color.x < .0f) color.x = .0f;
	if (color.y < .0f) color.y = .0f;
	if (color.z < .0f) color.z = .0f;

	pixel = (uint32_t)(color.x * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.y * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.z * 255.0f);

	return pixel;
}


static void draw_line(til_fb_fragment_t *fragment, int x1, int y1, int x2, int y2)
{
	int	x_delta = x2 - x1;
	int	y_delta = y2 - y1;
	int	sdx = x_delta < 0 ? -1 : 1;
	int	sdy = y_delta < 0 ? -1 : 1;

	x_delta = abs(x_delta);
	y_delta = abs(y_delta);

	if (x_delta >= y_delta) {
		/* X-major */
		for (int minor = 0, x = 0; x <= x_delta; x++, x1 += sdx, minor += y_delta) {
			if (minor >= x_delta) {
				y1 += sdy;
				minor -= x_delta;
			}

			til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x1, y1, 0xffffffff);
		}
	} else {
		/* Y-major */
		for (int minor = 0, y = 0; y <= y_delta; y++, y1 += sdy, minor += x_delta) {
			if (minor >= y_delta) {
				x1 += sdx;
				minor -= y_delta;
			}

			til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x1, y1, 0xffffffff);
		}
	}
}

#define ZCONST	3.f

static void draw_polyhedron(const polyhedron_t *polyhedron, m4f_t *transform, til_fb_fragment_t *fragment)
{
	unsigned	n_faces = polyhedron->edge_cnt - polyhedron->vertex_cnt + 2;	// https://en.wikipedia.org/wiki/Euler%27s_polyhedron_formula
	unsigned	n_verts_per_face = polyhedron->n_vertices / n_faces;
	const v3f_t	*v = polyhedron->vertices, *_v;

	for (unsigned f = 0; f < n_faces; f++) {
		_v = v + n_verts_per_face - 1;
		for (unsigned i = 0; i < n_verts_per_face; i++, v++) {
			int	x1, y1, x2, y2;
			v3f_t	xv, _xv;

			_xv = m4f_mult_v3f(transform, _v);
			xv = m4f_mult_v3f(transform, v);

			x1 = _xv.x / (_xv.z + ZCONST) * fragment->frame_width + fragment->frame_width * .5f;
			y1 = _xv.y / (_xv.z + ZCONST) * fragment->frame_height + fragment->frame_height * .5f;

			x2 = xv.x / (xv.z + ZCONST) * fragment->frame_width + fragment->frame_width * .5f;
			y2 = xv.y / (xv.z + ZCONST) * fragment->frame_height + fragment->frame_height * .5f;

			draw_line(fragment, x1, y1, x2, y2);
			_v = v;
		}
	}
}


static til_module_context_t * plato_create_context(til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	plato_context_t	*ctxt;

	ctxt = til_module_context_new(stream, sizeof(plato_context_t), seed, ticks, n_cpus, path);
	if (!ctxt)
		return NULL;

	ctxt->setup = *((plato_setup_t *)setup);

	ctxt->taps.spin_rate = til_tap_init_float(&ctxt->spin_rate, 1, &ctxt->setup.spin_rate, "spin_rate");
	ctxt->taps.orbit_rate = til_tap_init_float(&ctxt->orbit_rate, 1, &ctxt->setup.orbit_rate, "orbit_rate");

	return &ctxt->til_module_context;
}


static void plato_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	plato_context_t		*ctxt = (plato_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	/* since we don't automate the rates ourselves, we don't care about the tap return values */
	(void) til_stream_tap_context(stream, context, &ctxt->taps.orbit_rate);
	(void) til_stream_tap_context(stream, context, &ctxt->taps.spin_rate);

	ctxt->r += (float)(ticks - context->ticks) * (*ctxt->orbit_rate * .001f);
	ctxt->rr += (float)(ticks - context->ticks) * (*ctxt->spin_rate * .001f);
	context->ticks = ticks;
	til_fb_fragment_clear(fragment);

	for (int i = 0; i < sizeof(polyhedra) / sizeof(*polyhedra); i++) {
		m4f_t	transform;
		float	p = (M_PI * 2.f) / 5.f, l;
		v3f_t	ax;

		p *= (float)i;
		p -= ctxt->r;

		/* tweak the rotation axis */
		ax.x = cosf(p);
		ax.y = sinf(p);
		ax.z = cosf(p) * sinf(p);

		/* normalize rotation vector, open-coded here */
		l = 1.f/sqrtf(ax.x*ax.x+ax.y*ax.y+ax.z*ax.z);
		ax.x *= l;
		ax.y *= l;
		ax.z *= l;

		/* arrange the solids on a circle, at points of a pentagram */
		transform = m4f_translate(NULL, &(v3f_t){cosf(p), sinf(p), 0.f});
		transform = m4f_scale(&transform, &(v3f_t){.5f, .5f, .5f});
		transform = m4f_rotate(&transform, &ax, ctxt->rr);

		draw_polyhedron(polyhedra[i], &transform, fragment);
	}
}


static int plato_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*orbit_rate;
	const char	*spin_rate;
	const char	*rate_values[] = {
				"-1",
				"-.75",
				"-.5",
				"-.25",
				".1",
				"0",
				".1",
				".25",
				".5",
				".75",
				"1",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Orbit rate and direction",
							.key = "orbit_rate",
							.regex = "\\.[0-9]+", /* FIXME */
							.preferred = TIL_SETTINGS_STR(PLATO_DEFAULT_ORBIT_RATE),
							.values = rate_values,
							.annotations = NULL
						},
						&orbit_rate,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Spin rate and direction",
							.key = "spin_rate",
							.regex = "\\.[0-9]+", /* FIXME */
							.preferred = TIL_SETTINGS_STR(PLATO_DEFAULT_SPIN_RATE),
							.values = rate_values,
							.annotations = NULL
						},
						&spin_rate,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		plato_setup_t	*setup;
		int		i;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		sscanf(orbit_rate, "%f", &setup->orbit_rate);
		sscanf(spin_rate, "%f", &setup->spin_rate);

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	plato_module = {
	.create_context = plato_create_context,
	.render_fragment = plato_render_fragment,
	.setup = plato_setup,
	.name = "plato",
	.description = "Platonic solids rendered in 3D",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
