#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_stream.h"
#include "til_tap.h"

#include "draw.h"

/* Copyright (C) 2017-20 Philip J. Freeman <elektron@halo.nu> */

#define DEFAULT_ROT_ADJ	.00003

struct points {
	float			x, y, z;
	struct			points *next;
};

typedef struct stars_context_t {
	til_module_context_t	til_module_context;
	struct			points* points;

	struct {
		til_tap_t		rot_rate;
		til_tap_t		rot_angle;
		til_tap_t		offset_x;
		til_tap_t		offset_y;
		til_tap_t		offset_angle;
	}			taps;

	struct {
		float			rot_rate;
		float			rot_angle;
		float			offset_x;
		float			offset_y;
		float			offset_angle;
	}			vars;

	float			rot_adj;
	float			*rot_rate;
	float			*rot_angle;
	float			*offset_x;
	float			*offset_y;
	float			*offset_angle;
	unsigned		seed;
} stars_context_t;

typedef struct stars_setup_t {
	til_setup_t		til_setup;
	float			rot_adj;
} stars_setup_t;


float get_random_unit_coord(unsigned *seed) {
	return (((float)rand_r(seed)/(float)RAND_MAX)*2.0)-1.0;
}


static til_module_context_t * stars_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	stars_context_t *ctxt;
	float		z;
	struct points* p_ptr = NULL;

	ctxt = til_module_context_new(module, sizeof(stars_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->points = NULL;
	ctxt->seed = seed;
	ctxt->rot_adj = ((stars_setup_t *)setup)->rot_adj;
	ctxt->vars.offset_x = 0.5;
	ctxt->vars.offset_angle = 0.01;

	ctxt->taps.rot_rate = til_tap_init_float(ctxt, &ctxt->rot_rate, 1, &ctxt->vars.rot_rate, "rot_rate");
	ctxt->taps.rot_angle = til_tap_init_float(ctxt, &ctxt->rot_angle, 1, &ctxt->vars.rot_angle, "rot_angle");
	ctxt->taps.offset_x = til_tap_init_float(ctxt, &ctxt->offset_x, 1, &ctxt->vars.offset_x, "offset_x");
	ctxt->taps.offset_y = til_tap_init_float(ctxt, &ctxt->offset_y, 1, &ctxt->vars.offset_y, "offset_y");
	ctxt->taps.offset_angle = til_tap_init_float(ctxt, &ctxt->offset_angle, 1, &ctxt->vars.offset_angle, "offset_angle");

	//add a bunch of points
	for(z=0.01; z<1; z=z+0.01) {
		for(int i=0; i<rand_r(&ctxt->seed)%16; i++){
			p_ptr = malloc(sizeof(struct points));
			if (!p_ptr)
				return NULL;
			p_ptr->x = get_random_unit_coord(&ctxt->seed);
			p_ptr->y = get_random_unit_coord(&ctxt->seed);
			p_ptr->z = z;
			p_ptr->next = ctxt->points;
			ctxt->points = p_ptr;
		}
	}
	return &ctxt->til_module_context;
}

static void stars_destroy_context(til_module_context_t *context)
{
	stars_context_t *ctxt = (stars_context_t *)context;
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


static void stars_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	stars_context_t		*ctxt = (stars_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	struct points* iterator;
	struct points* tmp_ptr;
	struct points* last_ptr=NULL;
	float		x, y, pos_x, pos_y, rot_x, rot_y, opacity, x_mult, y_mult, max_radius;
	int		width = fragment->frame_width, height = fragment->frame_height;
	float		dt = ((float)(ticks - context->last_ticks)) * .025f;

	if(width>height) {
		x_mult=1.f;
		y_mult=(float)width/(float)height;
	} else {
		x_mult=(float)height/(float)width;
		y_mult=1.f;
	}

	max_radius=1.f+((width+height)*.001f);

	til_fb_fragment_clear(fragment);

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

		rot_x = (x*cosf(*ctxt->rot_angle))-(y*sinf(*ctxt->rot_angle));
		rot_y = (x*sinf(*ctxt->rot_angle))+(y*cosf(*ctxt->rot_angle));

		pos_x = ((rot_x+*ctxt->offset_x+1.f)*.5f)*(float)width;
		pos_y = ((rot_y+*ctxt->offset_y+1.f)*.5f)*(float)height;

		if(iterator->z<0.1)
			opacity = iterator->z*10;
		else
			opacity = 1;

		if (pos_x>0 && pos_x<width && pos_y>0 && pos_y<height)
			til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, pos_x, pos_y,
				makergb(0xFF, 0xFF, 0xFF, opacity));

		for(int my_y=floorf(pos_y-max_radius); my_y<=(int)ceilf(pos_y+max_radius); my_y++)
		for(int my_x=floorf(pos_x-max_radius); my_x<=(int)ceilf(pos_x+max_radius); my_x++) {

			//Is the point within our viewing window?
			if (!(my_x>0 && my_x<width && my_y>0 && my_y<height))
				continue;

			//Is the point within the circle?
			if(powf(my_x-pos_x, 2)+powf(my_y-pos_y, 2) > powf((iterator->z*max_radius), 2))
				continue;


			til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, my_x, my_y,
				makergb(0xFF, 0xFF, 0xFF, opacity));

		}

		iterator->z += 0.01 * dt;
		last_ptr=iterator;
		iterator=iterator->next;
	}

	if (dt > 0.f) {
		// add stars at horizon
		for(int i=0; i<rand_r(&ctxt->seed)%16; i++){
			tmp_ptr = malloc(sizeof(struct points));
			if (!tmp_ptr)
				break;
			tmp_ptr->x = get_random_unit_coord(&ctxt->seed);
			tmp_ptr->y = get_random_unit_coord(&ctxt->seed);
			tmp_ptr->z = 0.01;
			tmp_ptr->next = ctxt->points;
			ctxt->points = tmp_ptr;
		}

		if (!til_stream_tap_context(stream, context, NULL, &ctxt->taps.rot_angle))
			*ctxt->rot_angle += (*ctxt->rot_rate * dt);

		if (!til_stream_tap_context(stream, context, NULL, &ctxt->taps.rot_rate)) {
			// handle rotation parameters
			if(*ctxt->rot_angle>M_PI_4)
				*ctxt->rot_rate = *ctxt->rot_rate - (ctxt->rot_adj * dt);
			else
				*ctxt->rot_rate = *ctxt->rot_rate + (ctxt->rot_adj * dt);
		}

		/* there's no automation of offset_angle */
		(void) til_stream_tap_context(stream, context, NULL, &ctxt->taps.offset_angle);

		// handle offset parameters
		if (!til_stream_tap_context(stream, context, NULL, &ctxt->taps.offset_x)) {
			float tmp_x = (*ctxt->offset_x*cosf(*ctxt->offset_angle * dt))-
					(*ctxt->offset_y*sinf(*ctxt->offset_angle * dt));
			*ctxt->offset_x = tmp_x;
		}

		if (!til_stream_tap_context(stream, context, NULL, &ctxt->taps.offset_y)) {
			float tmp_y = (*ctxt->offset_x*sinf(*ctxt->offset_angle * dt))+
					(*ctxt->offset_y*cosf(*ctxt->offset_angle * dt));
			*ctxt->offset_y = tmp_y;
		}

	}
}

int stars_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*rot_adj;
	const char	*rot_adj_values[] = {
				".0",
				".00001",
				".00003",
				".0001",
				".0003",
				".001",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Rotation rate",
							.key = "rot_adj",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(DEFAULT_ROT_ADJ),
							.values = rot_adj_values,
							.annotations = NULL
						},
						&rot_adj,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		stars_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL);
		if (!setup)
			return -ENOMEM;

		sscanf(rot_adj, "%f", &setup->rot_adj);

		*res_setup = &setup->til_setup;
	}

	return 0;
}

til_module_t	stars_module = {
	.create_context  = stars_create_context,
	.destroy_context = stars_destroy_context,
	.render_fragment = stars_render_fragment,
	.setup = stars_setup,
	.name = "stars",
	.description = "Basic starfield",
	.author = "Philip J Freeman <elektron@halo.nu>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
