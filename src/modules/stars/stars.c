#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>

#include "draw.h"
#include "fb.h"
#include "rototiller.h"

/* Copyright (C) 2017-19 Philip J. Freeman <elektron@halo.nu> */

struct points
{
	float x, y, z;
	struct points *next;
};


typedef struct stars_context_t {
	struct		points* points;
} stars_context_t;


float get_random_unit_coord() {
	return (((float)rand()/(float)RAND_MAX)*2.0)-1.0;
}


static void * stars_create_context(unsigned num_cpus)
{
	stars_context_t *ctxt;
	float		z;
	struct points* p_ptr = NULL;

	ctxt = malloc(sizeof(stars_context_t));
	if (!ctxt)
		return NULL;

	ctxt->points = NULL;

	//add a bunch of points
	for(z=0.01; z<1; z=z+0.01) {
		for(int i=0; i<rand()%16; i++){
			p_ptr = malloc(sizeof(struct points));
			if (!p_ptr)
				return NULL;
			p_ptr->x = get_random_unit_coord();
			p_ptr->y = get_random_unit_coord();
			p_ptr->z = z;
			p_ptr->next = ctxt->points;
			ctxt->points = p_ptr;
		}
	}
	return ctxt;
}

static void stars_destroy_context(void *context)
{
	stars_context_t *ctxt = context;
	struct points* p_ptr;
	struct points* last_ptr=NULL;

	for ( p_ptr=ctxt->points; p_ptr != NULL; p_ptr = p_ptr->next)
	{
		if (last_ptr!=NULL)
			free(last_ptr);

		last_ptr=p_ptr;
	}

	free(last_ptr);

	free(context);
}


static void stars_render_fragment(void *context, unsigned cpu, fb_fragment_t *fragment)
{
	stars_context_t	*ctxt = context;
	struct points* iterator;
	struct points* tmp_ptr;
	struct points* last_ptr=NULL;
	float		x, y, pos_x, pos_y, opacity;
	int		width = fragment->width, height = fragment->height;


	float max_radius=1.f+((width+height)*.001f);

	fb_fragment_zero(fragment);

	iterator=ctxt->points;
	for(;;)
	{
		if(iterator == NULL)
			break;

		if(iterator->z >= 1) {
			if(last_ptr == NULL)
				ctxt->points = iterator->next;
			else
				last_ptr->next = iterator->next;
			tmp_ptr = iterator;
			iterator = iterator->next;
			free(tmp_ptr);
			continue;
		}

		x = iterator->x / (1.f - iterator->z);
		y = iterator->y / (1.f - iterator->z);

		pos_x = ((x+1.f)*.5f)*(float)width;
		pos_y = ((y+1.f)*.5f)*(float)height;

		if(iterator->z<0.1)
			opacity = iterator->z*10;
		else
			opacity = 1;

		if (pos_x>0 && pos_x<width && pos_y>0 && pos_y<height)
			fb_fragment_put_pixel_unchecked(fragment, pos_x, pos_y,
				makergb(0xFF, 0xFF, 0xFF, opacity));

		for(int my_y=floorf(pos_y-max_radius); my_y<=ceilf(pos_y+max_radius); my_y++)
		for(int my_x=floorf(pos_x-max_radius); my_x<=ceilf(pos_x+max_radius); my_x++) {

			//Is the point within our viewing window?
			if (!(my_x>0 && my_x<width && my_y>0 && my_y<height))
				continue;

			//Is the point within the circle?
			if(powf(my_x-pos_x, 2)+powf(my_y-pos_y, 2) > powf((iterator->z*max_radius), 2))
				continue;


			fb_fragment_put_pixel_unchecked(fragment, my_x, my_y,
				makergb(0xFF, 0xFF, 0xFF, opacity));

		}

		iterator->z += 0.01;
		last_ptr=iterator;
		iterator=iterator->next;
	}

	// add stars at horizon
	for(int i=0; i<rand()%16; i++){
		tmp_ptr = malloc(sizeof(struct points));
		if (!tmp_ptr)
			break;
		tmp_ptr->x = get_random_unit_coord();
		tmp_ptr->y = get_random_unit_coord();
		tmp_ptr->z = 0.01;
		tmp_ptr->next = ctxt->points;
		ctxt->points = tmp_ptr;
	}
}

rototiller_module_t	stars_module = {
	.create_context  = stars_create_context,
	.destroy_context = stars_destroy_context,
	.render_fragment = stars_render_fragment,
	.name = "stars",
	.description = "Basic starfield",
	.author = "Philip J Freeman <elektron@halo.nu>",
	.license = "GPLv2",
};
