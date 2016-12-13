#include <stdint.h>
#include <inttypes.h>
#include <math.h>

#include "fb.h"
#include "rototiller.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

/* Some defines for the fixed-point stuff in render(). */
#define FIXED_TRIG_LUT_SIZE	4096	/* size of the cos/sin look-up tables */
#define FIXED_BITS		12	/* fractional bits */
#define FIXED_EXP		4096	/* 2^FIXED_BITS */
#define FIXED_COS(_rad)		costab[_rad % FIXED_TRIG_LUT_SIZE]
#define FIXED_SIN(_rad)		sintab[_rad % FIXED_TRIG_LUT_SIZE]
#define FIXED_MULT(_a, _b)	((_a * _b) >> FIXED_BITS)
#define FIXED_NEW(_i)		(_i << FIXED_BITS)
#define FIXED_TO_INT(_f)	((_f) >> FIXED_BITS)


/* Draw a rotating checkered 256x256 texture into fragment. (32-bit version) */
static void roto32(fb_fragment_t *fragment)
{
	static int32_t	costab[FIXED_TRIG_LUT_SIZE], sintab[FIXED_TRIG_LUT_SIZE];
	static uint8_t	texture[256][256];
	static int	initialized;
	static uint32_t	colors[2];
	static unsigned	r, rr;

	int		y_cos_r, y_sin_r, x_cos_r, x_sin_r, x_cos_r_init, x_sin_r_init, cos_r, sin_r;
	int		x, y, stride = fragment->stride / 4, width = fragment->width, height = fragment->height;
	uint8_t		tx, ty; /* 256x256 texture; 8 bit texture indices to modulo via overflow. */
	uint32_t	*buf = fragment->buf;

	if (!initialized) {
		int i;

		initialized = 1;

		/* Generate simple checker pattern texture, nothing clever, feel free to play! */
		/* If you modify texture on every frame instead of only @ initialization you can
		 * produce some neat output.  These values are indexed into colors[] below. */
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

	/* This is all done using fixed-point in the hopes of being faster, and yes assumptions
	 * are being made WRT the overflow of tx/ty as well, only tested on x86_64. */
	cos_r = FIXED_COS(r);
	sin_r = FIXED_SIN(r);

	/* Vary the colors, this is just a mashup of sinusoidal rgb values. */
	colors[0] =	((FIXED_TO_INT(FIXED_MULT(FIXED_COS(rr), FIXED_NEW(127))) + 128) << 16) |
			((FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr / 2), FIXED_NEW(127))) + 128) << 8) |
			((FIXED_TO_INT(FIXED_MULT(FIXED_COS(rr / 3), FIXED_NEW(127))) + 128));

	colors[1] =	((FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr / 2), FIXED_NEW(127))) + 128) << 16) |
			((FIXED_TO_INT(FIXED_MULT(FIXED_COS(rr / 2), FIXED_NEW(127))) + 128)) << 8 |
			((FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr), FIXED_NEW(127))) + 128) );

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

			tx = FIXED_TO_INT(x_sin_r - y_cos_r);
			ty = FIXED_TO_INT(y_sin_r + x_cos_r);

			*buf = colors[texture[ty][tx]];

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
static void roto64(fb_fragment_t *fragment)
{
	static int32_t	costab[FIXED_TRIG_LUT_SIZE], sintab[FIXED_TRIG_LUT_SIZE];
	static uint8_t	texture[256][256];
	static int	initialized;
	static uint32_t	colors[2];
	static unsigned	r, rr;

	int		y_cos_r, y_sin_r, x_cos_r, x_sin_r, x_cos_r_init, x_sin_r_init, cos_r, sin_r;
	int		x, y, stride = fragment->stride / 8, width = fragment->width, height = fragment->height;
	uint8_t		tx, ty; /* 256x256 texture; 8 bit texture indices to modulo via overflow. */
	uint64_t	*buf = (uint64_t *)fragment->buf;

	if (!initialized) {
		int i;

		initialized = 1;

		/* Generate simple checker pattern texture, nothing clever, feel free to play! */
		/* If you modify texture on every frame instead of only @ initialization you can
		 * produce some neat output.  These values are indexed into colors[] below. */
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

	/* This is all done using fixed-point in the hopes of being faster, and yes assumptions
	 * are being made WRT the overflow of tx/ty as well, only tested on x86_64. */
	cos_r = FIXED_COS(r);
	sin_r = FIXED_SIN(r);

	/* Vary the colors, this is just a mashup of sinusoidal rgb values. */
	colors[0] =	((FIXED_TO_INT(FIXED_MULT(FIXED_COS(rr), FIXED_NEW(127))) + 128) << 16) |
			((FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr / 2), FIXED_NEW(127))) + 128) << 8) |
			((FIXED_TO_INT(FIXED_MULT(FIXED_COS(rr / 3), FIXED_NEW(127))) + 128));

	colors[1] =	((FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr / 2), FIXED_NEW(127))) + 128) << 16) |
			((FIXED_TO_INT(FIXED_MULT(FIXED_COS(rr / 2), FIXED_NEW(127))) + 128)) << 8 |
			((FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr), FIXED_NEW(127))) + 128) );

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

			tx = FIXED_TO_INT(x_sin_r - y_cos_r);
			ty = FIXED_TO_INT(y_sin_r + x_cos_r);

			p = colors[texture[ty][tx]];

			x_cos_r += cos_r;
			x_sin_r += sin_r;

			tx = FIXED_TO_INT(x_sin_r - y_cos_r);
			ty = FIXED_TO_INT(y_sin_r + x_cos_r);

			p |= (uint64_t)colors[texture[ty][tx]] << 32;
			
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


rototiller_renderer_t	roto32_renderer = {
	.render = roto32,
	.name = "roto32",
	.description = "Tiled texture rotation (32-bit)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};


rototiller_renderer_t	roto64_renderer = {
	.render = roto64,
	.name = "roto64",
	.description = "Tiled texture rotation (64-bit)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
