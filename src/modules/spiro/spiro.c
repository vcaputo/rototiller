#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_stream.h"
#include "til_tap.h"

#include "draw.h"

/* Copyright (C) 2020 Philip J. Freeman <elektron@halo.nu> */

/*

Spirograph Emulator

  refs:

    - https://en.wikipedia.org/wiki/Spirograph#Mathematical_basis
    - https://en.wikipedia.org/wiki/Unit_circle#Trigonometric_functions_on_the_unit_circle
*/

typedef struct spiro_context_t {
	til_module_context_t	til_module_context;
	float			r;
	int			r_dir;
	float			p;
	int			p_dir;

	struct {
		til_tap_t		l;
		til_tap_t		k;
		til_tap_t		rounds;
	}			taps;

	struct {
		float			l;
		float			k;
		float			rounds;
	}			vars;

	float			*l;
	float			*k;
	float			*rounds;
} spiro_context_t;

static void spiro_update_taps(spiro_context_t *ctxt, til_stream_t *stream, float dt)
{

	if (dt > 0.f) {
		/* FIXME: these increments should be scaled by a delta-t,
		 * but at least by filtering on same-tick things don't go
		 * crazy in multi-drawn context scenarios like checkers::fill_module
		 */
		/* check bounds and increment r & p */
		float next_r=ctxt->r+(.00001f*ctxt->r_dir);
		if(next_r >= 1.f || next_r <= 0.f || next_r <= ctxt->p)
			ctxt->r_dir=ctxt->r_dir*-1;
		else
			ctxt->r=ctxt->r+(.00001f*ctxt->r_dir);

		float next_p=ctxt->p+(.0003f*ctxt->p_dir);
		if(next_p >= ctxt->r || next_p <= 0)
			ctxt->p_dir=ctxt->p_dir*-1;
		else
			ctxt->p=ctxt->p+(.0003f*ctxt->p_dir);
	}

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.l))
		*ctxt->l = ctxt->p/ctxt->r;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.k))
		*ctxt->k = ctxt->r;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.rounds))
		*ctxt->rounds = 128;
}

static til_module_context_t * spiro_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	spiro_context_t *ctxt;

	ctxt = til_module_context_new(module, sizeof(spiro_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->r=.25f+(rand_r(&seed)/(float)RAND_MAX)*.5f;
	if(ctxt->r>.5f)
		ctxt->r_dir=-1;
	else
		ctxt->r_dir=1;
	ctxt->p=(rand_r(&seed)/(float)RAND_MAX)*ctxt->r;
	ctxt->p_dir=ctxt->r_dir*-1;
#ifdef DEBUG
	printf("spiro: initial context: r=%f, dir=%i, p=%f, dir=%i\n", ctxt->r, ctxt->r_dir, ctxt->p, ctxt->p_dir);
#endif
	ctxt->taps.l = til_tap_init_float(ctxt, &ctxt->l, 1, &ctxt->vars.l, "l");
	ctxt->taps.k = til_tap_init_float(ctxt, &ctxt->k, 1, &ctxt->vars.k, "k");
	ctxt->taps.rounds = til_tap_init_float(ctxt, &ctxt->rounds, 1, &ctxt->vars.rounds, "rounds");

	spiro_update_taps(ctxt, stream, 0.f);

	return &ctxt->til_module_context;
}

static void spiro_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	spiro_context_t		*ctxt = (spiro_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	int	width = fragment->frame_width, height = fragment->frame_height;
	int	display_R, display_origin_x, display_origin_y;
	float	dt = ((float)(ticks - context->last_ticks)) * .001f;

	spiro_update_taps(ctxt, stream, dt);

	/* Based on the fragment's dimensions, calculate the origin and radius of the fixed outer
	circle, C0. */

	if(width>=height) {			 // landscape or square aspect ratio
		display_R=(height-1)*0.5f;
		display_origin_x=((width-height)*.5f)+display_R;
		display_origin_y=display_R;
	} else {				// portrait
		display_R=(width-1)*.5f;
		display_origin_x=display_R;
		display_origin_y=((height-width)*.5f)+display_R;
	}

	/* blank the fragment */
	til_fb_fragment_clear(fragment);

	/* plot one spirograph run */
	float l=*ctxt->l;
	float k=*ctxt->k;
	float incr=M_PI/display_R;
	for(float t=0.f; t<*ctxt->rounds*2*M_PI; t+= incr) {
		float my_x=((1.f-k)*cosf(t))+(l*k*cosf(((1.f-k)/k)*t));
		float my_y=((1.f-k)*sinf(t))-(l*k*sinf(((1.f-k)/k)*t));
		int pos_x=display_origin_x+(my_x*display_R);
		int pos_y=display_origin_y+(my_y*display_R);
		til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, pos_x, pos_y,
			fragment->texture ? 0xffffffff :
				makergb(sinf(M_1_PI*t)*127+128,
					sinf(M_1_PI*t+(2*M_PI*.333333333333f))*127+128,
					sinf(M_1_PI*t+(4*M_PI*.333333333333f))*127+128,
					0.76));
	}

#ifdef DEBUG
	/* plot the origin point */
	til_fb_fragment_put_pixel_checked(fragment, 0, display_origin_x, display_origin_y,
		makergb(0xFF, 0xFF, 0x00, 1));

	/* plot the fixed outer circle C0 */
	for(float a=0.f; a<2*M_PI; a+= M_PI_2/display_R) {
		int pos_x=display_origin_x+(cosf(a)*display_R);
		int pos_y=display_origin_y+(sinf(a)*display_R);
		til_fb_fragment_put_pixel_checked(fragment, 0, pos_x, pos_y,
			makergb(0xFF, 0xFF, 0x00, 1));
	}

	/* plot inner circle Ci */
	til_fb_fragment_put_pixel_checked(fragment, 0, display_origin_x+display_R-(ctxt->r*display_R),
		display_origin_y, makergb(0xFF, 0xFF, 0x00, 1));

	for(float a=0.f; a<2*M_PI; a+= M_PI_2/display_R) {
		int pos_x=display_origin_x+display_R-(ctxt->r*display_R)+
			(cosf(a)*ctxt->r*display_R);
		int pos_y=display_origin_y+(sinf(a)*ctxt->r*display_R);
		til_fb_fragment_put_pixel_checked(fragment, 0, pos_x, pos_y,
			makergb(0xFF, 0xFF, 0x00, 1));
	}

	/* plot p */
	til_fb_fragment_put_pixel_checked(fragment, 0, display_origin_x+display_R-(ctxt->r*display_R)+
		(ctxt->p*display_R), display_origin_y, makergb(0xFF, 0xFF, 0x00, 1));
#endif

}

til_module_t	spiro_module = {
	.create_context  = spiro_create_context,
	.render_fragment = spiro_render_fragment,
	.name = "spiro",
	.description = "Spirograph emulator",
	.author = "Philip J Freeman <elektron@halo.nu>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
