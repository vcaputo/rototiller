#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "til.h"
#include "til_fb.h"

/* Copyright (C) 2017 Vito Caputo <vcaputo@pengaru.com> */

/* Normalize plasma size at 2*8K resolution, simply assume it's always being sampled
 * smaller than this and ignore handling <1 fractional scaling factors.
 */
#define PLASMA_WIDTH		15360
#define PLASMA_HEIGHT		8640

#define FIXED_TRIG_LUT_SIZE	4096			/* size of the cos/sin look-up tables */
#define FIXED_BITS		9			/* fractional bits */
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
	/* Generate fixed-point cos & sin LUTs. */
	for (int i = 0; i < FIXED_TRIG_LUT_SIZE; i++) {
		costab[i] = ((cos((double)2*M_PI*i/FIXED_TRIG_LUT_SIZE))*FIXED_EXP);
		sintab[i] = ((sin((double)2*M_PI*i/FIXED_TRIG_LUT_SIZE))*FIXED_EXP);
	}
}


static void * plasma_create_context(unsigned ticks, unsigned num_cpus, til_setup_t *setup)
{
	static int	initialized;

	if (!initialized) {
		initialized = 1;

		init_plasma(costab, sintab);
	}

	return calloc(1, sizeof(plasma_context_t));
}


static void plasma_destroy_context(void *context)
{
	free(context);
}


static int plasma_fragmenter(void *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	plasma_context_t	*ctxt = context;

	return til_fb_fragment_slice_single(fragment, ctxt->n_cpus, number, res_fragment);
}


/* Prepare a frame for concurrent drawing of fragment using multiple fragments */
static void plasma_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter)
{
	plasma_context_t	*ctxt = context;

	*res_fragmenter = plasma_fragmenter;
	ctxt->n_cpus = n_cpus;
	ctxt->rr += 3;
}


/* Draw a plasma effect */
static void plasma_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	plasma_context_t	*ctxt = context;
	int			xstep = PLASMA_WIDTH / fragment->frame_width;
	int			ystep = PLASMA_HEIGHT / fragment->frame_height;
	unsigned		width = fragment->width * xstep, height = fragment->height * ystep;
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

	for (y = fragment->y * ystep; y < fragment->y * ystep + height; y += ystep) {
		int	y2 = y << 1;
		int	y4 = y << 2;

		dy2 = cy - y;
		dy2 *= dy2;

		for (x = fragment->x * xstep; x < fragment->x * xstep + width; x += xstep, buf++) {
			int	v;
			int	hyp;

			dx2 = cx - x;
			dx2 *= dx2;

			hyp = (dx2 + dy2) >> 13;	/* XXX: technically this should be a sqrt(), but >> 10 is a whole lot faster. */
#define S	4
			v = FIXED_MULT(	((FIXED_COS(rr8 + ((hyp * 5) >> S))) +
					(FIXED_SIN(-rr16 + ((x << 2) >> S))) +
					(FIXED_COS(rr20 + (y4 >> S)))),
					FIXED_EXP / 3);	/* XXX: note these '/ 3' get optimized out. */
			c.r = FIXED_MULT(v, cscale.r) + cscale.r;

			v = FIXED_MULT(	((FIXED_COS(rr12 + ((hyp << 2) >> S))) +
					(FIXED_COS(rr6 + ((x << 1) >> S))) +
					(FIXED_SIN(rr16 + (y2 >> S)))),
					FIXED_EXP / 3);
			c.g = FIXED_MULT(v, cscale.g) + cscale.g;

			v = FIXED_MULT(	((FIXED_SIN(rr6 + ((hyp * 6) >> S))) +
					(FIXED_COS(-rr12 + ((x * 5) >> S))) +
					(FIXED_SIN(-rr6 + (y2 >> S)))),
					FIXED_EXP / 3);
			c.b = FIXED_MULT(v, cscale.b) + cscale.b;

			*buf = color2pixel(&c);
		}

		buf = ((void *)buf) + fragment->stride;
	}
}


til_module_t	plasma_module = {
	.create_context = plasma_create_context,
	.destroy_context = plasma_destroy_context,
	.prepare_frame = plasma_prepare_frame,
	.render_fragment = plasma_render_fragment,
	.name = "plasma",
	.description = "Oldskool plasma effect (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
