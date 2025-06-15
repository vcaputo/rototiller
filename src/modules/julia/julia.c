#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

/* Copyright (C) 2017 Vito Caputo <vcaputo@pengaru.com> */

/* Julia set renderer - see https://en.wikipedia.org/wiki/Julia_set, morphing just means to vary C. */

/* TODO: explore using C99 complex.h and its types? */

typedef struct julia_context_t {
	til_module_context_t	til_module_context;
	float			rr;
	float			realscale;
	float			imagscale;
	float			creal;
	float			cimag;
	float			threshold;
} julia_context_t;

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


static til_module_context_t * julia_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	julia_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(julia_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->rr = ((float)rand_r(&seed)) / (float)RAND_MAX * 100.f;

	return &ctxt->til_module_context;
}


static inline unsigned julia_iter(float real, float imag, float creal, float cimag, unsigned max_iters, float threshold)
{
	unsigned	i;
	float		newr, newi;

	for (i = 1; i < max_iters; i++) {
		newr = real * real - imag * imag;
		newi = imag * real;
		newi += newi;

		newr += creal;
		newi += cimag;

		if ((newr * newr + newi * newi) > threshold)
			return i;

		real = newr;
		imag = newi;
	}

	return 0;
}


/* Prepare a frame for concurrent drawing of fragment using multiple fragments */
static void julia_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	julia_context_t	*ctxt = (julia_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu_x16 };

	if (ticks != context->last_ticks) {
		/* TODO: this cumulative state in the context is problematic.
		 * It'd be better to absolutely derive this from ticks every prepare, so we could do things like
		 * rewind and jump around by changing ticks.  As-s, this is assuming rr advances a constant rate
		 * in a uniform direction.
		 * To behave better in multi-render situations like as a checkers fill_module, this was all made
		 * conditional on ticks differing from context->last_ticks, so it would at least suppress accumulating
		 * more movement when rendered more times in a given frame... but it should just all be reworked.
		 * roto has the same problem, these asssumptions are from a simpler time.
		 */
		ctxt->rr += .01;
				/* Rather than just sweeping creal,cimag from -2.0-+2.0, I try to keep things confined
				 * to an interesting (visually) range.  TODO: could certainly use refinement.
				 */
		ctxt->realscale = 0.01f * cosf(ctxt->rr) + 0.01f;
		ctxt->imagscale = 0.01f * sinf(ctxt->rr * 3.0f) + 0.01f;
		ctxt->creal = (1.01f + (ctxt->realscale * cosf(1.5f * M_PI + ctxt->rr) + ctxt->realscale)) * cosf(ctxt->rr * .3f);
		ctxt->cimag = (1.01f + (ctxt->imagscale * sinf(ctxt->rr * 3.0f) + ctxt->imagscale)) * sinf(ctxt->rr);

		/* Vary the divergent threshold, this has been tuned to dwell around 1 a bit since it's
		 * quite different looking, then shoot up to a very huge value approaching FLT_MAX which
		 * is also interesting.
		 */
		ctxt->threshold = cosf(M_PI + ctxt->rr * .1f) * .5f + .5f;
		ctxt->threshold *= ctxt->threshold * ctxt->threshold;
		ctxt->threshold *= 35.f;
		ctxt->threshold = powf(10.f, ctxt->threshold);
	}
}


/* Draw a morphing Julia set */
static void julia_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	julia_context_t		*ctxt = (julia_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	unsigned	x, y;
	unsigned	width = fragment->width, height = fragment->height;
	uint32_t	*buf = fragment->buf;
	float		real, imag;
	float		realstep = 3.6f / (float)fragment->frame_width, imagstep = 3.6f / (float)fragment->frame_height;


	/* Complex plane confined to {-1.8 - 1.8} on both axis (slightly zoomed), no dynamic zooming is performed. */
	for (imag = 1.8 + -(imagstep * (float)fragment->y), y = fragment->y; y < fragment->y + height; y++, imag += -imagstep) {
		for (real = -1.8 + realstep * (float)fragment->x, x = fragment->x; x < fragment->x + width; x++, buf++, real += realstep) {
			*buf = colors[julia_iter(real, imag, ctxt->creal, ctxt->cimag, sizeof(colors) / sizeof(*colors), ctxt->threshold)];
		}

		buf += fragment->stride;
	}
}


til_module_t	julia_module = {
	.create_context = julia_create_context,
	.prepare_frame = julia_prepare_frame,
	.render_fragment = julia_render_fragment,
	.name = "julia",
	.description = "Julia set fractal morpher (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
