#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "fb.h"
#include "rototiller.h"

/* Copyright (C) 2017 Vito Caputo <vcaputo@pengaru.com> */

#define FIXED_TRIG_LUT_SIZE	4096			/* size of the cos/sin look-up tables */
#define FIXED_BITS		10			/* fractional bits */
#define FIXED_EXP		(1 << FIXED_BITS)	/* 2^FIXED_BITS */
#define FIXED_MASK		(FIXED_EXP - 1)		/* fractional part mask */
#define FIXED_COS(_rad)		costab[(_rad) & (FIXED_TRIG_LUT_SIZE-1)]
#define FIXED_SIN(_rad)		sintab[(_rad) & (FIXED_TRIG_LUT_SIZE-1)]
#define FIXED_MULT(_a, _b)	(((_a) * (_b)) >> FIXED_BITS)
#define FIXED_NEW(_i)		((_i) << FIXED_BITS)
#define FIXED_TO_INT(_f)	((_f) >> FIXED_BITS)

typedef struct color_t {
	int	r, g, b;
} color_t;

static int32_t	costab[FIXED_TRIG_LUT_SIZE], sintab[FIXED_TRIG_LUT_SIZE];

typedef struct plasma_context_t {
	unsigned	rr;
	unsigned	n_cpus;
} plasma_context_t;

static inline uint32_t color2pixel(color_t *color)
{
	return (FIXED_TO_INT(color->r) << 16) | (FIXED_TO_INT(color->g) << 8) | FIXED_TO_INT(color->b);
}


static void init_plasma(int32_t *costab, int32_t *sintab)
{
	int	i;

	/* Generate fixed-point cos & sin LUTs. */
	for (i = 0; i < FIXED_TRIG_LUT_SIZE; i++) {
		costab[i] = ((cos((double)2*M_PI*i/FIXED_TRIG_LUT_SIZE))*FIXED_EXP);
		sintab[i] = ((sin((double)2*M_PI*i/FIXED_TRIG_LUT_SIZE))*FIXED_EXP);
	}
}


static void * plasma_create_context(void)
{
	return calloc(1, sizeof(plasma_context_t));
}


static void plasma_destroy_context(void *context)
{
	free(context);
}


static int plasma_fragmenter(void *context, const fb_fragment_t *fragment, unsigned num, fb_fragment_t *res_fragment)
{
	plasma_context_t	*ctxt = context;

	return fb_fragment_slice_single(fragment, ctxt->n_cpus, num, res_fragment);
}


/* Prepare a frame for concurrent drawing of fragment using multiple fragments */
static void plasma_prepare_frame(void *context, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter)
{
	plasma_context_t	*ctxt = context;
	static int		initialized;

	if (!initialized) {
		initialized = 1;

		init_plasma(costab, sintab);
	}

	*res_fragmenter = plasma_fragmenter;
	ctxt->n_cpus = n_cpus;
	ctxt->rr += 3;
}


/* Draw a plasma effect */
static void plasma_render_fragment(void *context, fb_fragment_t *fragment)
{
	plasma_context_t	*ctxt = context;
	unsigned		width = fragment->width, height = fragment->height;
	int			fw2 = FIXED_NEW(width / 2), fh2 = FIXED_NEW(height / 2);
	int			x, y, cx, cy, dx2, dy2;
	uint32_t		*buf = fragment->buf;
	color_t			c = { .r = 0, .g = 0, .b = 0 }, cscale;
	unsigned		rr2, rr6, rr8, rr16, rr20, rr12;

	rr2 = ctxt->rr * 2;
	rr6 = ctxt->rr * 6;
	rr8 = ctxt->rr * 8;
	rr16 = ctxt->rr * 16;
	rr20 = ctxt->rr * 20;
	rr12 = ctxt->rr * 12;

	/* vary the color channel intensities */
	cscale.r = FIXED_MULT(FIXED_COS(ctxt->rr / 2), FIXED_NEW(64)) + FIXED_NEW(64);
	cscale.g = FIXED_MULT(FIXED_COS(ctxt->rr / 5), FIXED_NEW(64)) + FIXED_NEW(64);
	cscale.b = FIXED_MULT(FIXED_COS(ctxt->rr / 7), FIXED_NEW(64)) + FIXED_NEW(64);

	cx = FIXED_TO_INT(FIXED_MULT(FIXED_COS(ctxt->rr), fw2) + fw2);
	cy = FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr2), fh2) + fh2);

	for (y = fragment->y; y < fragment->y + height; y++) {
		int	y2 = y << 1;
		int	y4 = y << 2;

		dy2 = cy - y;
		dy2 *= dy2;

		for (x = fragment->x; x < fragment->x + width; x++, buf++) {
			int	v;
			int	hyp;

			dx2 = cx - x;
			dx2 *= dx2;

			hyp = (dx2 + dy2) >> 10;	/* XXX: technically this should be a sqrt(), but >> 10 is a whole lot faster. */

			v = FIXED_MULT(	((FIXED_COS(rr8 + hyp * 5)) +
					(FIXED_SIN(-rr16 + (x << 2))) +
					(FIXED_COS(rr20 + y4))),
					FIXED_EXP / 3);	/* XXX: note these '/ 3' get optimized out. */
			c.r = FIXED_MULT(v, cscale.r) + cscale.r;

			v = FIXED_MULT(	((FIXED_COS(rr12 + (hyp << 2))) +
					(FIXED_COS(rr6 + (x << 1))) +
					(FIXED_SIN(rr16 + y2))),
					FIXED_EXP / 3);
			c.g = FIXED_MULT(v, cscale.g) + cscale.g;

			v = FIXED_MULT(	((FIXED_SIN(rr6 + hyp * 6)) +
					(FIXED_COS(-rr12 + x * 5)) +
					(FIXED_SIN(-rr6 + y2))),
					FIXED_EXP / 3);
			c.b = FIXED_MULT(v, cscale.b) + cscale.b;

			*buf = color2pixel(&c);
		}

		buf = ((void *)buf) + fragment->stride;
	}
}

rototiller_module_t	plasma_module = {
	.create_context = plasma_create_context,
	.destroy_context = plasma_destroy_context,
	.prepare_frame = plasma_prepare_frame,
	.render_fragment = plasma_render_fragment,
	.name = "plasma",
	.description = "Oldskool plasma effect (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
