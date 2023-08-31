#include <stdint.h>
#include <stdlib.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

#include "sig/sig.h"

/* Copyright (C) 2020-2022 - Vito Caputo <vcaputo@pengaru.com> */

/* 2D waveform drawings to exercise libs/sig and explore its ergonomics */

/* TODO: a tileable mode would be neat, where the start and end
 * heights always match up for the lines...
 *
 * TODO: a connected-lines version would be neat as well
 *
 * TODO: exposing taps for influencing the signals could be fun
 * too, as well as making N_SIGNALS a runtime setting.
 */

#define N_SIGNALS	11

typedef struct signals_context_t {
	til_module_context_t	til_module_context;
	sig_sig_t		*signals[N_SIGNALS];
} signals_context_t;


static til_module_context_t * signals_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	signals_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(signals_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->signals[0] = sig_new_sin(sig_new_const(.5f));			/* oscillate @ .5hz */
	if (!ctxt->signals[0])
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->signals[1] =	sig_new_sin(					/* oscillate */
					sig_new_scale(				/* at a scaled frequency */
						sig_new_sin(sig_new_const(.1f)),/* from another oscillator @ .1hz */
						sig_new_const(.2f),		/* from .2 */
						sig_new_const(7.f)		/* to 7 */
					)
				);
	if (!ctxt->signals[1])
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->signals[2] =	sig_new_lerp(					/* interpolate */
					sig_new_sin(sig_new_const(.33f)),	/* a .33hz oscillator */
					sig_new_sin(sig_new_const(.15f)),	/* and a .15hz oscillator */
					sig_new_sin(sig_new_const(2.f))		/* weighted by a 2hz oscillator */
				);
	if (!ctxt->signals[2])
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->signals[3] =	sig_new_pow(	       				/* raise */
					sig_new_sin(sig_new_const(4.f)),	/* a 4hz oscillator */
					sig_new_sin(sig_new_const(.33f))	/* to the power of a .33f hz oscillator */
				);
	if (!ctxt->signals[3])
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->signals[4] =	sig_new_mult(	       		       		/* multiply */
					sig_new_sin(sig_new_const(4.f)),	/* a 4hz oscillator */
					sig_new_sin(sig_new_const(1.f))		/* by a 1hz oscillator */
				);
	if (!ctxt->signals[4])
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->signals[5] =	sig_new_lerp(					/* interpolate */
					sig_ref(ctxt->signals[3]),		/* signals[3] */
					sig_ref(ctxt->signals[4]),		/* signals[4] */
					sig_ref(ctxt->signals[2])		/* weighted by signals[2] */
				);
	if (!ctxt->signals[5])
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->signals[6] =	sig_new_lerp(					/* interpolate */
					sig_new_inv(sig_ref(ctxt->signals[5])),	/* invert of signals[5] */
					sig_new_pow(sig_ref(ctxt->signals[5]),	/* with raised signals[5] */
						sig_ref(ctxt->signals[3])),	/* to power of signals[3] */
					sig_ref(ctxt->signals[2])		/* weighted by signals[2] */
				);
	if (!ctxt->signals[6])
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->signals[7] =	sig_new_mult(					/* multiply */
					sig_ref(ctxt->signals[6]),		/* signals[6] */
					sig_ref(ctxt->signals[5])		/* signals[5] */
				);
	if (!ctxt->signals[7])
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->signals[8] =	sig_new_mult(					/* multiply */
					sig_ref(ctxt->signals[1]),		/* signals[1] */
					sig_ref(ctxt->signals[7])		/* signals[7] */
				);
	if (!ctxt->signals[8])
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->signals[9] =	sig_new_pow(					/* raise */
					sig_ref(ctxt->signals[3]),		/* signals[3] */
					sig_new_scale(				/* to power of scaled */
						sig_ref(ctxt->signals[7]),	/* signals[7] */
						sig_new_const(.1f),		/* into range .1f .. */
						sig_new_const(20.f)		/* to 20.f */
					)
				);
	if (!ctxt->signals[9])
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->signals[10] =	sig_new_lerp(					/* interpolate */
					sig_ref(ctxt->signals[9]),		/* signals[9] */
					sig_new_rand(),				/* random noise */
					sig_new_lerp(				/* weighted by interpolating */
						sig_new_inv(			/* inverted ... */
							sig_ref(ctxt->signals[5])),/* signals[5] */
						sig_ref(ctxt->signals[3]),	/* and signals[3] */
						sig_ref(ctxt->signals[1])	/* weighted by signals[1] */
					)
				);
	if (!ctxt->signals[10])
		return til_module_context_free(&ctxt->til_module_context);

	return &ctxt->til_module_context;
}


static void signals_destroy_context(til_module_context_t *context)
{
	signals_context_t	*ctxt = (signals_context_t *)context;

	for (unsigned i = 0; i < N_SIGNALS; i++)
		sig_free(ctxt->signals[i]);

	free(ctxt);
}


static int signals_fragmenter(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	return til_fb_fragment_slice_single(fragment, N_SIGNALS, number, res_fragment);
}


static void signals_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	*res_frame_plan = (til_frame_plan_t){ .fragmenter = signals_fragmenter };
}


static void signals_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	signals_context_t	*ctxt = (signals_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	til_fb_fragment_clear(fragment);

	if (fragment->number >= N_SIGNALS)
		return;

	ticks >>= 2;	/* move a bit slower */

	for (unsigned x = 0, y; x < fragment->width; x++) {
		float	size = fragment->height - 1;

		/* This needs to compute an offset into fragment->height,
		 * from a 0-1 range, hence size is height - 1 to not overflow.
		 */
		y = size - sig_output(ctxt->signals[fragment->number], ticks + x) * size;

		til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, fragment->x + x, fragment->y + y, 0xffffffff);
	}
}


til_module_t	signals_module = {
	.create_context = signals_create_context,
	.destroy_context = signals_destroy_context,
	.prepare_frame = signals_prepare_frame,
	.render_fragment = signals_render_fragment,
	.name = "signals",
	.description = "2D Waveforms (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE | TIL_MODULE_EXPERIMENTAL,
};
