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
#include "settings.h"

/* Copyright (C) 2017-20 Philip J. Freeman <elektron@halo.nu> */

#define DEFAULT_ROT_ADJ	.00003f

static float	stars_rot_adj = DEFAULT_ROT_ADJ;

struct points
{
	float x, y, z;
	struct points *next;
};

typedef struct stars_context_t {
	struct		points* points;
	float		rot_adj;
	float		rot_rate;
	float		rot_angle;
	float		offset_x;
	float		offset_y;
	float		offset_angle;
} stars_context_t;


float get_random_unit_coord() {
	return (((float)rand()/(float)RAND_MAX)*2.0)-1.0;
}


static void * stars_create_context(unsigned ticks, unsigned num_cpus)
{
	stars_context_t *ctxt;
	float		z;
	struct points* p_ptr = NULL;

	ctxt = malloc(sizeof(stars_context_t));
	if (!ctxt)
		return NULL;

	ctxt->points = NULL;
	ctxt->rot_adj = stars_rot_adj;
	ctxt->rot_rate = 0.00;
	ctxt->rot_angle = 0;
	ctxt->offset_x = 0.5;
	ctxt->offset_y = 0;
	ctxt->offset_angle = 0.01;

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


static void stars_render_fragment(void *context, unsigned ticks, unsigned cpu, fb_fragment_t *fragment)
{
	stars_context_t	*ctxt = context;
	struct points* iterator;
	struct points* tmp_ptr;
	struct points* last_ptr=NULL;
	float		x, y, pos_x, pos_y, rot_x, rot_y, opacity, x_mult, y_mult, max_radius;
	int		width = fragment->width, height = fragment->height;

	if(width>height) {
		x_mult=1.f;
		y_mult=(float)width/(float)height;
	} else {
		x_mult=(float)height/(float)width;
		y_mult=1.f;
	}

	max_radius=1.f+((width+height)*.001f);

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

		x = (iterator->x / (1.f - iterator->z))*x_mult;
		y = (iterator->y / (1.f - iterator->z))*y_mult;

		rot_x = (x*cosf(ctxt->rot_angle))-(y*sinf(ctxt->rot_angle));
		rot_y = (x*sinf(ctxt->rot_angle))+(y*cosf(ctxt->rot_angle));

		pos_x = ((rot_x+ctxt->offset_x+1.f)*.5f)*(float)width;
		pos_y = ((rot_y+ctxt->offset_y+1.f)*.5f)*(float)height;

		if(iterator->z<0.1)
			opacity = iterator->z*10;
		else
			opacity = 1;

		if (pos_x>0 && pos_x<width && pos_y>0 && pos_y<height)
			fb_fragment_put_pixel_unchecked(fragment, pos_x, pos_y,
				makergb(0xFF, 0xFF, 0xFF, opacity));

		for(int my_y=floorf(pos_y-max_radius); my_y<=(int)ceilf(pos_y+max_radius); my_y++)
		for(int my_x=floorf(pos_x-max_radius); my_x<=(int)ceilf(pos_x+max_radius); my_x++) {

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

	// handle rotation parameters
	if(ctxt->rot_angle>M_PI_4)
		ctxt->rot_rate=ctxt->rot_rate-ctxt->rot_adj;
	else
		ctxt->rot_rate=ctxt->rot_rate+ctxt->rot_adj;
	ctxt->rot_angle=ctxt->rot_angle+ctxt->rot_rate;

	// handle offset parameters
	float tmp_x = (ctxt->offset_x*cosf(ctxt->offset_angle))-
			(ctxt->offset_y*sinf(ctxt->offset_angle));
	float tmp_y = (ctxt->offset_x*sinf(ctxt->offset_angle))+
			(ctxt->offset_y*cosf(ctxt->offset_angle));
	ctxt->offset_x = tmp_x;
	ctxt->offset_y = tmp_y;
}

int stars_setup(const settings_t *settings, setting_desc_t **next_setting)
{
	const char	*rot_adj;
	const char      *rot_adj_values[] = {
				".0f",
				".00001f",
				".00003f",
				".0001f",
				".0003f",
				".001f",
				NULL
			};

	rot_adj = settings_get_value(settings, "rot_adj");
	if(!rot_adj) {
		int ret_val;

		ret_val = setting_desc_clone(&(setting_desc_t){
							.name = "Rotation Rate",
							.key = "rot_adj",
							.regex = "\\.[0-9]+",
							.preferred = SETTINGS_STR(DEFAULT_ROT_ADJ),
							.values = rot_adj_values,
							.annotations = NULL
						}, next_setting);
		if(ret_val<0)
			return ret_val;

		return 1;
	}

	sscanf(rot_adj, "%f", &stars_rot_adj);

	return 0;
}

rototiller_module_t	stars_module = {
	.create_context  = stars_create_context,
	.destroy_context = stars_destroy_context,
	.render_fragment = stars_render_fragment,
	.setup = stars_setup,
	.name = "stars",
	.description = "Basic starfield",
	.author = "Philip J Freeman <elektron@halo.nu>",
	.license = "GPLv2",
};
