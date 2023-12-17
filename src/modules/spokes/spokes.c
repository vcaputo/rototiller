#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

#include "draw.h"

/* Copyright (C) 2022 Philip J. Freeman <elektron@halo.nu> */

#define SPOKES_DEFAULT_ITERATIONS	3
#define SPOKES_DEFAULT_TWIST		0.0625
#define SPOKES_DEFAULT_THICKNESS	3
#define SPOKES_DEFAULT_COUNT		4

typedef struct spokes_setup_t {
	til_setup_t             til_setup;
	unsigned		iterations;
	float                   twist;
	unsigned		thickness;
	unsigned		count;
} spokes_setup_t;

static void spokes_draw_line(til_fb_fragment_t *fragment, int x1, int y1, int x2, int y2, uint32_t color, int thickness)
{

	int x, y, offset;

	if(x1==x2 && y1==y2) {
		til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x1, y1, color);
		return;
	}

	int dx=x2-x1;
	int dy=y2-y1;

	if(abs(dx)>=abs(dy)) { /* iterate along x */
		float rate=(float)dy/(float)dx;
		if(dx>0) {
			for(x=x1; x<=x2; x++) {
				y=y1+round((x-x1)*rate);
				if(y>=0 && y<fragment->height)
					til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, color);
				for(offset=1; offset<thickness; offset++) {
					if(y+offset<fragment->height)
						til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y+offset, color);
					if(y-offset>=0)
						til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y-offset, color);
				}
			}
		} else {
			for(x=x1; x>=x2; x--) {
				y=y1+round((x-x1)*rate);
				if(y>=0 && y<fragment->height)
					til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, color);
				for(offset=1; offset<thickness; offset++) {
					if(y+offset<fragment->height)
						til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y+offset, color);
					if(y-offset>=0)
						til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y-offset, color);
				}
			}
		}

	} else { /* iterate along y */
		float rate=(float)dx/(float)dy;
		if(dy>0) {
			for(y=y1; y<=y2; y++) {
				x=x1+round((y-y1)*rate);
				if(x>=0 && x<fragment->width)
					til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, color);
				for(offset=1; offset<thickness; offset++) {
					if(x+offset<fragment->width)
						til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x+offset, y, color);
					if(x-offset>=0)
						til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x-offset, y, color);
				}
			}
		} else {
			for(y=y1; y>=y2; y--) {
				x=x1+round((y-y1)*rate);
				if(x>=0 && x<fragment->width)
					til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, color);
				for(offset=1; offset<thickness; offset++) {
					if(x+offset<fragment->width)
						til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x+offset, y, color);
					if(x-offset>=0)
						til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x-offset, y, color);
				}
			}
		}
	}
}

static void spokes_draw_segmented_line(til_fb_fragment_t *fragment, int iterations, double theta, int x1, int y1, int x2, int y2, uint32_t color, int thickness)
{
	/* recurse until iterations == 0 */

	if(iterations>0) {
		int midpoint_x=((x1+x2)/2);
		int midpoint_y=((y1+y2)/2);

		midpoint_x=cos(theta)*(midpoint_x-x1)-sin(theta)*(midpoint_y-y1)+x1;
		midpoint_y=sin(theta)*(midpoint_x-x1)+cos(theta)*(midpoint_y-y1)+y1;

		/* Check if any of the midpoints are outside of the drawable area and fix them. */
		if (midpoint_x<0) midpoint_x=0;
		if (midpoint_x>=fragment->width) midpoint_x=fragment->width-1;
		if (midpoint_y<0) midpoint_y=0;
		if (midpoint_y>=fragment->height) midpoint_y=fragment->height-1;

		spokes_draw_segmented_line(fragment, iterations-1, theta*0.5, x1, y1, midpoint_x, midpoint_y, color, thickness);
		spokes_draw_segmented_line(fragment, iterations-1, theta*-0.5, x2, y2, midpoint_x, midpoint_y, color, thickness);
		return;
	}
	spokes_draw_line(fragment, x1, y1, x2, y2, color, thickness);
}

static void spokes_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	til_fb_fragment_t       *fragment = *fragment_ptr;

	int width = fragment->width, height = fragment->height;
	float stride;
	float offset;
	uint32_t color;

	int display_R, display_origin_x, display_origin_y;
	int origin_x, origin_y;

	spokes_setup_t *s = (spokes_setup_t *)context->setup;

	/* calculate theta for twist */
	double theta=M_PI*s->twist;

	/* Based on the fragment's dimensions, calculate the origin and radius of the largest
	circle that can fully fit in the frame */

	if(width>=height) {
		display_R=(height-1)*0.5f;
		display_origin_x=((width-height)*.5f)+display_R;
		display_origin_y=display_R;
	} else {
		display_R=(width-1)*0.5f;
		display_origin_x=display_R;
		display_origin_y=((height-width)*.5f)+display_R;
	}

	/* calculate a moving origin for all the lines in this frame based on ticks */
	origin_x=display_origin_x+cos((float)ticks*0.001)*display_R*0.7f;
	origin_y=display_origin_y+sin((float)ticks*0.001)*display_R*0.7f;

	/* calculate an offset for outer line endpoints based on ticks */
	offset=(float)ticks*0.1f; /* use the ticks. */

	/* rotate through RGB color space slowly based on ticks */
	color=makergb(
		(int)round(255.0*sinf((float)ticks*0.00001)),
		(int)round(255.0*sinf((float)ticks*0.00001+0.6667*M_PI)),
		(int)round(255.0*sinf((float)ticks*0.00001+1.3333*M_PI)),
		1);

	stride = ((float)(width+height))/(float)s->count;

	/* we're setup now to draw some shit */
	til_fb_fragment_clear(fragment);

	for(float i=0; floorf(i)<=((float)width+height)-stride; i+=stride){ /* iterate over half the perimiter at a time... */
		float perimiter_x, perimiter_y;

		if(i+fmodf(offset, stride) < width) {
			perimiter_x=i+fmodf(offset, stride);
			perimiter_y=0;
		} else {
			perimiter_x=width-1;
			perimiter_y=(i-((float)width))+fmodf(offset, stride);
		}

		spokes_draw_segmented_line(fragment, s->iterations, theta, origin_x, origin_y, rintf(perimiter_x), rintf(perimiter_y), color, s->thickness);

		/* Calculate and draw the mirror line... */
		perimiter_x=fabsf(perimiter_x-width);
		perimiter_y=fabsf(perimiter_y-height);
		spokes_draw_segmented_line(fragment, s->iterations, theta, origin_x, origin_y, rintf(perimiter_x), rintf(perimiter_y), color, s->thickness);
	}
}


int spokes_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);

til_module_t	spokes_module = {
	.render_fragment = spokes_render_fragment,
	.setup = spokes_setup,
	.name = "spokes",
	.description = "Twisted spokes",
	.author = "Philip J Freeman <elektron@halo.nu>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


int spokes_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*iterations;
	const char	*iterations_values[] = {
				"1",
				"2",
				"3",
				"4",
				NULL
			};
	til_setting_t	*twist;
	const char	*twist_values[] = {
				"-4.0",
				"-2.0",
				"-1.0",
				"-0.5",
				"-0.25",
				"-0.125",
				"-0.0625",
				"-0.03125",
				"-0.015125",
				"0.0",
				"0.015125",
				"0.03125",
				"0.0625",
				"0.125",
				"0.25",
				"0.5",
				"1.0",
				"2.0",
				"4.0",
				NULL
			};
	til_setting_t	*thickness;
	const char	*thickness_values[] = {
				"1",
				"2",
				"3",
				"5",
				NULL
			};
	til_setting_t	*count;
	const char	*count_values[] = {
				"2",
				"3",
				"4",
				"8",
				"10",
				"15",
				"20",
				"25",
				"30",
				"40",
				NULL
			};
	int r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Iterations",
							.key = "iterations",
							.regex = "[0-9]+",
							.preferred = TIL_SETTINGS_STR(SPOKES_DEFAULT_ITERATIONS),
							.values = iterations_values,
							.annotations = NULL
						},
						&iterations,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Number of spokes (gets doubled)",
							.key = "count",
							.regex = "[0-9]+",
							.preferred = TIL_SETTINGS_STR(SPOKES_DEFAULT_COUNT),
							.values = count_values,
							.annotations = NULL
						},
						&count,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Twist",
							.key = "twist",
							.regex = "[0-9]+\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(SPOKES_DEFAULT_TWIST),
							.values = twist_values,
							.annotations = NULL
						},
						&twist,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Thickness",
							.key = "thickness",
							.regex = "[0-9]+",
							.preferred = TIL_SETTINGS_STR(SPOKES_DEFAULT_THICKNESS),
							.values = thickness_values,
							.annotations = NULL
						},
						&thickness,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		spokes_setup_t	*setup;

		setup = til_setup_new(settings,sizeof(*setup), NULL, &spokes_module);
		if (!setup)
			return -ENOMEM;

		if (sscanf(iterations->value, "%u", &setup->iterations) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, iterations, res_setting, -EINVAL);

		if (sscanf(count->value, "%u", &setup->count) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, count, res_setting, -EINVAL);

		if (sscanf(twist->value, "%f", &setup->twist) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, twist, res_setting, -EINVAL);

		if (sscanf(thickness->value, "%u", &setup->thickness) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, thickness, res_setting, -EINVAL);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
