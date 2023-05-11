#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

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

typedef struct roto_context_t {
	til_module_context_t	til_module_context;
	unsigned		r, rr;
	color_t			palette[2];
} roto_context_t;

static int32_t	costab[FIXED_TRIG_LUT_SIZE], sintab[FIXED_TRIG_LUT_SIZE];
static uint8_t	texture[256][256];


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


static til_module_context_t * roto_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	static int	initialized;
	roto_context_t	*ctxt;

	if (!initialized) {
		initialized = 1;

		init_roto(texture, costab, sintab);
	}

	ctxt = til_module_context_new(module, sizeof(roto_context_t), stream, seed, ticks, n_cpus, path, setup);
	if (!ctxt)
		return NULL;

	ctxt->r = rand_r(&seed);
	ctxt->rr = rand_r(&seed);

	return &ctxt->til_module_context;
}


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
		if (ne == sw && sw == se)
			return (FIXED_TO_INT(palette[sw].r) << 16) | (FIXED_TO_INT(palette[sw].g) << 8) | FIXED_TO_INT(palette[sw].b);

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


/* prepare a frame for concurrent rendering */
static void roto_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	roto_context_t	*ctxt = (roto_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu };

	// This governs the rotation and color cycle.
	if (ticks != context->ticks) {
		ctxt->r += FIXED_TO_INT(FIXED_MULT(FIXED_SIN(ctxt->rr), FIXED_NEW(16)));
		ctxt->rr += (ticks - context->ticks) >> 2;

		/* Vary the colors, this is just a mashup of sinusoidal rgb values. */
		ctxt->palette[0].r = (FIXED_MULT(FIXED_COS(ctxt->rr), FIXED_NEW(127)) + FIXED_NEW(128));
		ctxt->palette[0].g = (FIXED_MULT(FIXED_SIN(ctxt->rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
		ctxt->palette[0].b = (FIXED_MULT(FIXED_COS(ctxt->rr / 3), FIXED_NEW(127)) + FIXED_NEW(128));

		ctxt->palette[1].r = (FIXED_MULT(FIXED_SIN(ctxt->rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
		ctxt->palette[1].g = (FIXED_MULT(FIXED_COS(ctxt->rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
		ctxt->palette[1].b = (FIXED_MULT(FIXED_SIN(ctxt->rr), FIXED_NEW(127)) + FIXED_NEW(128));

		context->ticks = ticks;
	}
}


/* Draw a rotating checkered 256x256 texture into fragment. */
static void roto_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	roto_context_t		*ctxt = (roto_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	int		x, y, frame_width = fragment->frame_width, frame_height = fragment->frame_height;
	int		y_cos_r, y_sin_r, x_cos_r, x_sin_r, x_cos_r_init, x_sin_r_init, cos_r, sin_r;
	uint32_t	*buf = fragment->buf;

	/* This is all done using fixed-point in the hopes of being faster, and yes assumptions
	 * are being made WRT the overflow of tx/ty as well, only tested on x86_64. */
	cos_r = FIXED_COS(ctxt->r);
	sin_r = FIXED_SIN(ctxt->r);

	/* The dimensions are cut in half and negated to center the rotation. */
	/* The [xy]_{sin,cos}_r variables are accumulators to replace multiplication with addition. */
	x_cos_r_init = FIXED_MULT(-FIXED_NEW(frame_width / 2) + FIXED_NEW(fragment->x), cos_r);
	x_sin_r_init = FIXED_MULT(-FIXED_NEW(frame_width / 2) + FIXED_NEW(fragment->x), sin_r);

	y_cos_r = FIXED_MULT(-FIXED_NEW(frame_height / 2) + FIXED_NEW(fragment->y), cos_r);
	y_sin_r = FIXED_MULT(-FIXED_NEW(frame_height / 2) + FIXED_NEW(fragment->y), sin_r);

	for (y = fragment->y; y < fragment->y + fragment->height; y++) {

		x_cos_r = x_cos_r_init;
		x_sin_r = x_sin_r_init;

		for (x = fragment->x; x < fragment->x + fragment->width; x++, buf++) {
			*buf = bilerp_color(texture, ctxt->palette, x_sin_r - y_cos_r, y_sin_r + x_cos_r);

			x_cos_r += cos_r;
			x_sin_r += sin_r;
		}

		buf += fragment->stride;
		y_cos_r += cos_r;
		y_sin_r += sin_r;
	}
}


til_module_t	roto_module = {
	.create_context = roto_create_context,
	.prepare_frame = roto_prepare_frame,
	.render_fragment = roto_render_fragment,
	.name = "roto",
	.description = "Anti-aliased tiled texture rotation (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
