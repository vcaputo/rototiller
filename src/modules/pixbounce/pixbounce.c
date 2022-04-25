#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "til.h"
#include "draw.h"

/* Copyright (C) 2018-19 Philip J. Freeman <elektron@halo.nu> */

int pix_width = 16;
int pix_height = 16;
int num_pix = 4;

char pix_map[][16*16] = {

	{
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
	0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
	0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
	0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
	0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0,
	1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
	0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0,
	0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0,
	0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
	0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	},

	{
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	},

	{
	0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
	0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
	0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
	0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
	0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
	0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
	0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
	0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
	},

	{
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
	0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0,
	0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0,
	0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0,
	0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0,
	1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1,
	1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1,
	1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1,
	1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1,
	0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0,
	0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0,
	0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0,
	0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	},
};

typedef struct pixbounce_context_t {
	unsigned	n_cpus;
	int		x, y;
	int		x_dir, y_dir;
	int		pix_num;

} pixbounce_context_t;

/* randomly pick another pixmap from the list, excluding the current one. */
static int pick_pix(int num_pics, int last_pic)
{
	int pix_num = last_pic;

	while(pix_num == last_pic)
		pix_num = rand() % num_pics;

	return pix_num;
}

static void * pixbounce_create_context(unsigned ticks, unsigned num_cpus, til_setup_t *setup)
{
	pixbounce_context_t *ctxt;

	ctxt = malloc(sizeof(pixbounce_context_t));
	if (!ctxt)
		return NULL;

	ctxt->n_cpus = num_cpus;
	ctxt->x = 0;
	ctxt->y = 8;
	ctxt->x_dir = 1;
	ctxt->y_dir = 1;
	ctxt->pix_num = rand() % num_pix;

	return ctxt;
}

static void pixbounce_destroy_context(void *context)
{
	pixbounce_context_t *ctxt = context;
	free(context);
}

static void pixbounce_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	pixbounce_context_t *ctxt = context;

	int	multiplier_x, multiplier_y, multiplier;
	int	width = fragment->width, height = fragment->height;

	/* blank the frame */
	til_fb_fragment_clear(fragment);

	/* check for very small fragment */
	if(pix_width*2>width||pix_height*2>height)
		return;

	/* calculate multiplyer for the pixmap */
	multiplier_x = width / pix_width;
	multiplier_y = height / pix_height;

	if(multiplier_x>=multiplier_y) {
		multiplier = multiplier_y * 77 / 100;
	} else if(multiplier_y>multiplier_x) {
		multiplier = multiplier_x * 77 / 100;
	}

	/* translate pixmap to multiplier size and draw it to the fragment */
	for(int cursor_y=0; cursor_y < pix_height*multiplier; cursor_y++) {
		for(int cursor_x=0; cursor_x < pix_width*multiplier; cursor_x++) {
			int pix_offset = ((cursor_y/multiplier)*pix_width) + (cursor_x/multiplier);
			if(pix_map[ctxt->pix_num][pix_offset] == 0) continue;
			til_fb_fragment_put_pixel_unchecked(
					fragment, ctxt->x+cursor_x, ctxt->y+cursor_y,
					makergb(0xFF, 0xFF, 0xFF, 1)
				);
		}
	}

	/* update pixmap location */
	if(ctxt->x+ctxt->x_dir < 0) {
		ctxt->x_dir = 1;
		ctxt->pix_num = pick_pix(num_pix, ctxt->pix_num);
	}
	if((ctxt->x+(pix_width*multiplier))+ctxt->x_dir > width) {
		ctxt->x_dir = -1;
		ctxt->pix_num = pick_pix(num_pix, ctxt->pix_num);
	}
	if(ctxt->y+ctxt->y_dir < 0) {
		ctxt->y_dir = 1;
		ctxt->pix_num = pick_pix(num_pix, ctxt->pix_num);
	}
	if((ctxt->y+(pix_height*multiplier))+ctxt->y_dir > height) {
		ctxt->y_dir = -1;
		ctxt->pix_num = pick_pix(num_pix, ctxt->pix_num);
	}
	ctxt->x = ctxt->x+ctxt->x_dir;
	ctxt->y = ctxt->y+ctxt->y_dir;
}

til_module_t	pixbounce_module = {
	.create_context  = pixbounce_create_context,
	.destroy_context = pixbounce_destroy_context,
	.render_fragment = pixbounce_render_fragment,
	.name = "pixbounce",
	.description = "Pixmap bounce",
	.author = "Philip J Freeman <elektron@halo.nu>",
};
