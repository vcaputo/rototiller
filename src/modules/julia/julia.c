#include <stdint.h>
#include <inttypes.h>
#include <math.h>

#include "fb.h"
#include "rototiller.h"

/* Copyright (C) 2017 Vito Caputo <vcaputo@pengaru.com> */

/* Julia set renderer - see https://en.wikipedia.org/wiki/Julia_set, morphing just means to vary C. */

/* TODO: explore using C99 complex.h and its types? */

static float	rr;
static float	realscale;
static float	imagscale;
static float	creal;
static float	cimag;

static inline unsigned julia_iter(float real, float imag, float creal, float cimag, unsigned max_iters)
{
	unsigned	i;
	float		newr, newi;

	for (i = 1; i < max_iters; i++) {
		newr = real * real - imag * imag;
		newi = imag * real;
		newi += newi;

		newr += creal;
		newi += cimag;

		if ((newr * newr + newi * newi) > 4.0)
			return i;

		real = newr;
		imag = newi;
	}

	return 0;
}


/* Prepare a frame for concurrent drawing of fragment using multiple fragments */
static void julia_prepare_frame(unsigned n_cpus, fb_fragment_t *fragment, rototiller_frame_t *res_frame)
{
	res_frame->n_fragments = n_cpus;
	fb_fragment_divide(fragment, n_cpus, res_frame->fragments);

	rr += .01;
			/* Rather than just sweeping creal,cimag from -2.0-+2.0, I try to keep things confined
			 * to an interesting (visually) range.  TODO: could certainly use refinement.
			 */
	realscale = 0.01f * cosf(rr) + 0.01f;
	imagscale = 0.01f * sinf(rr * 3.0f) + 0.01f;
	creal = (1.01f + (realscale * cosf(1.5f*M_PI+rr) + realscale)) * cosf(rr * .3f);
	cimag = (1.01f + (imagscale * sinf(rr * 3.0f) + imagscale)) * sinf(rr);
}


/* Draw a morphing Julia set */
static void julia_render_fragment(fb_fragment_t *fragment)
{
	static uint32_t	colors[] = {
				/* this palette is just something I slapped together, definitely needs improvement. TODO */
				0x000000,
				0x000044,
				0x000088,
				0x0000aa,
				0x0000ff,
				0x0044ff,
				0x0088ff,
				0x00aaff,
				0x00ffff,
				0x44ffaa,
				0x88ff88,
				0xaaff44,
				0xffff00,
				0xffaa00,
				0xff8800,
				0xff4400,
				0xff0000,
				0xaa0000,
				0x880000,
				0x440000,
				0x440044,
				0x880088,
				0xaa00aa,
				0xff00ff,
				0xff4400,
				0xff8800,
				0xffaa00,
				0xffff00,
				0xaaff44,
				0x88ff88,
				0x44ffaa,
				0x00ffff,
				0x00aaff,
				0x0088ff,
				0xff4400,
				0xff00ff,
				0xaa00aa,
				0x880088,
				0x440044,
			};

	unsigned	x, y;
	unsigned	stride = fragment->stride / 4, width = fragment->width, height = fragment->height;
	uint32_t	*buf = fragment->buf;
	float		real, imag;
	float		realstep = 3.6f / (float)fragment->frame_width, imagstep = 3.6f / (float)fragment->frame_height;


	/* Complex plane confined to {-1.8 - 1.8} on both axis (slightly zoomed), no dynamic zooming is performed. */
	for (imag = 1.8 + -(imagstep * (float)fragment->y), y = fragment->y; y < fragment->y + height; y++, imag += -imagstep) {
		for (real = -1.8 + realstep * (float)fragment->x, x = fragment->x; x < fragment->x + width; x++, buf++, real += realstep) {
			*buf = colors[julia_iter(real, imag, creal, cimag, sizeof(colors) / sizeof(*colors))];
		}

		buf += stride;
	}
}

rototiller_module_t	julia_module = {
	.prepare_frame = julia_prepare_frame,
	.render_fragment = julia_render_fragment,
	.name = "julia",
	.description = "Julia set fractal morpher (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
