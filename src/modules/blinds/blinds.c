#include <math.h>
#include <stdio.h>
#include <string.h>

#include "til.h"
#include "til_fb.h"

/* Copyright (C) 2017-2022 Vito Caputo <vcaputo@pengaru.com> */

#define	NUM_BLINDS	16

/* draw a blind over fragment */
static inline void draw_blind(til_fb_fragment_t *fragment, unsigned row, float t)
{
	unsigned	row_height = fragment->frame_height / NUM_BLINDS;
	unsigned	height = roundf(t * (float)row_height);

	for (unsigned y = 0; y < height; y++)
		memset(fragment->buf + ((row * row_height) + y ) * (fragment->pitch >> 2), 0xff, fragment->width * 4);
}


/* draw blinds over the fragment */
static void blinds_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	static float rr;

	unsigned	row;
	float		r;

	til_fb_fragment_clear(fragment);

	for (r = rr, row = 0; row < NUM_BLINDS; row++, r += .1)
		draw_blind(fragment, row, 1.f - fabsf(cosf(r)));

	rr += .01;
}


til_module_t	blinds_module = {
	.render_fragment = blinds_render_fragment,
	.name = "blinds",
	.description = "Retro 80s-inspired window blinds",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
