#include <stdint.h>
#include <inttypes.h>
#include <math.h>

#include "fb.h"
#include "rototiller.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

/* Some defines for the fixed-point stuff in render(). */
#define FIXED_TRIG_LUT_SIZE	4096			/* size of the cos/sin look-up tables */
#define FIXED_BITS		11			/* fractional bits */
#define FIXED_EXP		(1 << FIXED_BITS)	/* 2^FIXED_BITS */
#define FIXED_MASK		(FIXED_EXP - 1)		/* fractional part mask */
#define FIXED_COS(_rad)		costab[(_rad) % FIXED_TRIG_LUT_SIZE]
#define FIXED_SIN(_rad)		sintab[(_rad) % FIXED_TRIG_LUT_SIZE]
#define FIXED_MULT(_a, _b)	(((_a) * (_b)) >> FIXED_BITS)
#define FIXED_NEW(_i)		((_i) << FIXED_BITS)
#define FIXED_TO_INT(_f)	((_f) >> FIXED_BITS)

typedef struct color_t {
	int	r, g, b;
} color_t;


/* linearly interpolate between two colors, alpha is fixed-point value 0-FIXED_EXP. */
static inline color_t lerp_color(color_t *a, color_t *b, int alpha)
{
	/* TODO: This could be done without multiplies with a bit of effort,
	 * maybe a simple table mapping integer color deltas to shift values
	 * for shifting alpha which then gets simply added?  A table may not even
	 * be necessary, use the order of the delta to derive how much to shift
	 * alpha?
	 */
	color_t	c = {
			.r = a->r + FIXED_MULT(alpha, b->r - a->r),
			.g = a->g + FIXED_MULT(alpha, b->g - a->g),
			.b = a->b + FIXED_MULT(alpha, b->b - a->b),
		};

	return c;
}


/* Return the bilinearly interpolated color palette[texture[ty][tx]] (Anti-Aliasing) */
/* tx, ty are fixed-point for fractions, palette colors are also in fixed-point format. */
static uint32_t bilerp_color(uint8_t texture[256][256], color_t *palette, int tx, int ty)
{
	uint8_t	itx = FIXED_TO_INT(tx), ity = FIXED_TO_INT(ty);
	color_t	n_color, s_color, color;
	int	x_alpha, y_alpha;
	uint8_t	nw, ne, sw, se;

	/* We need the 4 texels constituting a 2x2 square pattern to interpolate.
	 * A point tx,ty can only intersect one texel; one corner of the 2x2 square.
	 * Where relative to the corner's center the intersection occurs determines which corner has been intersected,
	 * and the other corner texels may then be addressed relative to that corner.
	 * Alpha values must also be determined for both axis, these values describe the position between
	 * the 2x2 texel centers the intersection occurred, aka the weight or bias.
	 * Once the two alpha values are known, linear interpolation between the texel colors is trivial.
	 */

	if ((ty & FIXED_MASK) > (FIXED_EXP >> 1)) {
		y_alpha = ty & (FIXED_MASK >> 1);

		if ((tx & (FIXED_MASK)) > (FIXED_EXP >> 1)) {
			nw = texture[ity][itx];
			ne = texture[ity][(uint8_t)(itx + 1)];
			sw = texture[(uint8_t)(ity + 1)][itx];
			se = texture[(uint8_t)(ity + 1)][(uint8_t)(itx + 1)];

			x_alpha = tx & (FIXED_MASK >> 1);
		} else {
			ne = texture[ity][itx];
			nw = texture[ity][(uint8_t)(itx - 1)];
			se = texture[(uint8_t)(ity + 1)][itx];
			sw = texture[(uint8_t)(ity + 1)][(uint8_t)(itx - 1)];

			x_alpha = (FIXED_EXP >> 1) + (tx & (FIXED_MASK >> 1));
		}
	} else {
		y_alpha = (FIXED_EXP >> 1) + (ty & (FIXED_MASK >> 1));

		if ((tx & (FIXED_MASK)) > (FIXED_EXP >> 1)) {
			sw = texture[ity][itx];
			se = texture[ity][(uint8_t)(itx + 1)];
			nw = texture[(uint8_t)(ity - 1)][itx];
			ne = texture[(uint8_t)(ity - 1)][(uint8_t)(itx + 1)];

			x_alpha = tx & (FIXED_MASK >> 1);
		} else {
			se = texture[ity][itx];
			sw = texture[ity][(uint8_t)(itx - 1)];
			ne = texture[(uint8_t)(ity - 1)][itx];
			nw = texture[(uint8_t)(ity - 1)][(uint8_t)(itx - 1)];

			x_alpha = (FIXED_EXP >> 1) + (tx & (FIXED_MASK >> 1));
		}
	}

	/* Skip interpolation of same colors, a substantial optimization with plain textures like the checker pattern */
	if (nw == ne) {
		if (ne == sw && sw == se) {
			return (FIXED_TO_INT(palette[sw].r) << 16) | (FIXED_TO_INT(palette[sw].g) << 8) | FIXED_TO_INT(palette[sw].b);
		}
		n_color = palette[nw];
	} else {
		n_color = lerp_color(&palette[nw], &palette[ne], x_alpha);
	}

	if (sw == se) {
		s_color = palette[sw];
	} else {
		s_color = lerp_color(&palette[sw], &palette[se], x_alpha);
	}

	color = lerp_color(&n_color, &s_color, y_alpha);

	return (FIXED_TO_INT(color.r) << 16) | (FIXED_TO_INT(color.g) << 8) | FIXED_TO_INT(color.b);
}


static void init_roto(uint8_t texture[256][256], int32_t *costab, int32_t *sintab)
{
	int	x, y, i;

	/* Generate simple checker pattern texture, nothing clever, feel free to play! */
	/* If you modify texture on every frame instead of only @ initialization you can
	 * produce some neat output.  These values are indexed into palette[] below. */
	for (y = 0; y < 128; y++) {
		for (x = 0; x < 128; x++)
			texture[y][x] = 1;
		for (; x < 256; x++)
			texture[y][x] = 0;
	}
	for (; y < 256; y++) {
		for (x = 0; x < 128; x++)
			texture[y][x] = 0;
		for (; x < 256; x++)
			texture[y][x] = 1;
	}

	/* Generate fixed-point cos & sin LUTs. */
	for (i = 0; i < FIXED_TRIG_LUT_SIZE; i++) {
		costab[i] = ((cos((double)2*M_PI*i/FIXED_TRIG_LUT_SIZE))*FIXED_EXP);
		sintab[i] = ((sin((double)2*M_PI*i/FIXED_TRIG_LUT_SIZE))*FIXED_EXP);
	}
}


/* Draw a rotating checkered 256x256 texture into fragment. (32-bit version) */
static void roto32_render_fragment(fb_fragment_t *fragment)
{
	static int32_t	costab[FIXED_TRIG_LUT_SIZE], sintab[FIXED_TRIG_LUT_SIZE];
	static uint8_t	texture[256][256];
	static int	initialized;
	static color_t	palette[2];
	static unsigned	r, rr;

	int		y_cos_r, y_sin_r, x_cos_r, x_sin_r, x_cos_r_init, x_sin_r_init, cos_r, sin_r;
	int		x, y, stride = fragment->stride / 4, width = fragment->width, height = fragment->height;
	uint32_t	*buf = fragment->buf;

	if (!initialized) {
		initialized = 1;

		init_roto(texture, costab, sintab);
	}

	/* This is all done using fixed-point in the hopes of being faster, and yes assumptions
	 * are being made WRT the overflow of tx/ty as well, only tested on x86_64. */
	cos_r = FIXED_COS(r);
	sin_r = FIXED_SIN(r);

	/* Vary the colors, this is just a mashup of sinusoidal rgb values. */
	palette[0].r = (FIXED_MULT(FIXED_COS(rr), FIXED_NEW(127)) + FIXED_NEW(128));
	palette[0].g = (FIXED_MULT(FIXED_SIN(rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
	palette[0].b = (FIXED_MULT(FIXED_COS(rr / 3), FIXED_NEW(127)) + FIXED_NEW(128));

	palette[1].r = (FIXED_MULT(FIXED_SIN(rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
	palette[1].g = (FIXED_MULT(FIXED_COS(rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
	palette[1].b = (FIXED_MULT(FIXED_SIN(rr), FIXED_NEW(127)) + FIXED_NEW(128));

	/* The dimensions are cut in half and negated to center the rotation. */
	/* The [xy]_{sin,cos}_r variables are accumulators to replace multiplication with addition. */
	x_cos_r_init = FIXED_MULT(-FIXED_NEW((width / 2)), cos_r);
	x_sin_r_init = FIXED_MULT(-FIXED_NEW((width / 2)), sin_r);

	y_cos_r = FIXED_MULT(-FIXED_NEW((height / 2)), cos_r);
	y_sin_r = FIXED_MULT(-FIXED_NEW((height / 2)), sin_r);

	for (y = 0; y < height; y++) {

		x_cos_r = x_cos_r_init;
		x_sin_r = x_sin_r_init;

		for (x = 0; x < width; x++, buf++) {
			*buf = bilerp_color(texture, palette, x_sin_r - y_cos_r, y_sin_r + x_cos_r);

			x_cos_r += cos_r;
			x_sin_r += sin_r;
		}

		buf += stride;
		y_cos_r += cos_r;
		y_sin_r += sin_r;
	}

	// This governs the rotation and color cycle.
	r += FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr), FIXED_NEW(16)));
	rr += 2;
}


/* Draw a rotating checkered 256x256 texture into fragment. (64-bit version) */
static void roto64_render_fragment(fb_fragment_t *fragment)
{
	static int32_t	costab[FIXED_TRIG_LUT_SIZE], sintab[FIXED_TRIG_LUT_SIZE];
	static uint8_t	texture[256][256];
	static int	initialized;
	static color_t	palette[2];
	static unsigned	r, rr;

	int		y_cos_r, y_sin_r, x_cos_r, x_sin_r, x_cos_r_init, x_sin_r_init, cos_r, sin_r;
	int		x, y, stride = fragment->stride / 8, width = fragment->width, height = fragment->height;
	uint64_t	*buf = (uint64_t *)fragment->buf;

	if (!initialized) {
		initialized = 1;

		init_roto(texture, costab, sintab);
	}

	/* This is all done using fixed-point in the hopes of being faster, and yes assumptions
	 * are being made WRT the overflow of tx/ty as well, only tested on x86_64. */
	cos_r = FIXED_COS(r);
	sin_r = FIXED_SIN(r);

	/* Vary the colors, this is just a mashup of sinusoidal rgb values. */
	palette[0].r = (FIXED_MULT(FIXED_COS(rr), FIXED_NEW(127)) + FIXED_NEW(128));
	palette[0].g = (FIXED_MULT(FIXED_SIN(rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
	palette[0].b = (FIXED_MULT(FIXED_COS(rr / 3), FIXED_NEW(127)) + FIXED_NEW(128));

	palette[1].r = (FIXED_MULT(FIXED_SIN(rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
	palette[1].g = (FIXED_MULT(FIXED_COS(rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
	palette[1].b = (FIXED_MULT(FIXED_SIN(rr), FIXED_NEW(127)) + FIXED_NEW(128));

	/* The dimensions are cut in half and negated to center the rotation. */
	/* The [xy]_{sin,cos}_r variables are accumulators to replace multiplication with addition. */
	x_cos_r_init = FIXED_MULT(-FIXED_NEW((width / 2)), cos_r);
	x_sin_r_init = FIXED_MULT(-FIXED_NEW((width / 2)), sin_r);

	y_cos_r = FIXED_MULT(-FIXED_NEW((height / 2)), cos_r);
	y_sin_r = FIXED_MULT(-FIXED_NEW((height / 2)), sin_r);

	width /= 2;	/* Since we're processing 64-bit words (2 pixels) at a time */

	for (y = 0; y < height; y++) {

		x_cos_r = x_cos_r_init;
		x_sin_r = x_sin_r_init;

		for (x = 0; x < width; x++, buf++) {
			uint64_t	p;

			p = bilerp_color(texture, palette, x_sin_r - y_cos_r, y_sin_r + x_cos_r);

			x_cos_r += cos_r;
			x_sin_r += sin_r;

			p |= (uint64_t)(bilerp_color(texture, palette, x_sin_r - y_cos_r, y_sin_r + x_cos_r)) << 32;

			*buf = p;

			x_cos_r += cos_r;
			x_sin_r += sin_r;
		}

		buf += stride;
		y_cos_r += cos_r;
		y_sin_r += sin_r;
	}

	// This governs the rotation and color cycle.
	r += FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr), FIXED_NEW(16)));
	rr += 2;
}


rototiller_module_t	roto32_module = {
	.render_fragment = roto32_render_fragment,
	.name = "roto32",
	.description = "Anti-aliased tiled texture rotation (32-bit)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};


rototiller_module_t	roto64_module = {
	.render_fragment = roto64_render_fragment,
	.name = "roto64",
	.description = "Anti-aliased tiled texture rotation (64-bit)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
