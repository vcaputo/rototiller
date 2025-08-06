/*
 *  Copyright (C) 2018 - Vito Caputo - <vcaputo@pengaru.com>
 *
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_util.h"

#include "grid/grid.h"

#define NUM_PLAYERS	8
#define GRID_SIZE	60
#define TICKS_PER_FRAME 8000

typedef struct color_t {
	float	r, g, b, a;
} color_t;

static color_t colors[NUM_PLAYERS + 1] = {
	{},			 	/* uninitialized cell starts black, becomes winner colors */
	{1.f, .317f, 0.f, 1.f },	/* orange */
	{.627f, .125f, 1.f, 1.f },	/* blue */
	{.878f, 0.f, 0.f, 1.f },	/* red */
	{.165f, .843f, .149f, 1.f },	/* green */
	{0.f, .878f, .815f, 1.f },	/* cyan */
	{.878f, 0.f, 1.f, 1.f },	/* purple */
	{.906f, .937f, 0.f, 1.f }, 	/* yellow */
	{}, 				/* black */
};


typedef struct submit_context_t {
	til_module_context_t	til_module_context;
	grid_t			*grid;
	grid_player_t		*players[NUM_PLAYERS];
	uint32_t		seq;
	uint32_t		game_winner;
	unsigned		bilerp:1;
	uint8_t			cells[GRID_SIZE * GRID_SIZE];
} submit_context_t;

typedef struct submit_setup_t {
	til_setup_t	til_setup;
	unsigned	bilerp:1;
} submit_setup_t;

/* convert a color into a packed, 32-bit rgb pixel value (taken from libs/ray/ray_color.h) */
static inline uint32_t color_to_uint32(color_t color) {
	uint32_t	pixel;

	if (color.r > 1.0f) color.r = 1.0f;
	if (color.g > 1.0f) color.g = 1.0f;
	if (color.b > 1.0f) color.b = 1.0f;
	if (color.a > 1.0f) color.a = 1.0f;

	pixel = (uint32_t)(color.a * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.r * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.g * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.b * 255.0f);

	return pixel;
}


static inline float clamp(float x, float lowerlimit, float upperlimit) {
	if (x < lowerlimit)
		x = lowerlimit;

	if (x > upperlimit)
		x = upperlimit;

	return x;
}


/* taken from https://en.wikipedia.org/wiki/Smoothstep#Variations */
static inline float smootherstep(float edge0, float edge1, float x) {
	x = clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);

	return x * x * x * (x * (x * 6.f - 15.f) + 10.f);
}


/* linearly interpolate colors */
static color_t color_lerp(color_t *a, color_t *b, float t)
{
	color_t	res = {
		.r = a->r * (1.f - t) + b->r * t,
		.g = a->g * (1.f - t) + b->g * t,
		.b = a->b * (1.f - t) + b->b * t,
		.a = a->a * (1.f - t) + b->a * t,
	};

	return res;
}


/* bilinearly interpolate colors from 4 cells */
static inline uint32_t sample_grid_bilerp(submit_context_t *ctxt, float x, float y)
{
	int		i, ix = x, iy = y;
	float		x_t, y_t;
	uint8_t		corners[2][2];
	color_t		x1, x2;

	i = iy * GRID_SIZE + ix;

	/* ix,iy forms the corner of a 2x2 kernel, determine which corner */
	if (x > ix + .5f) {
		x_t = x - ((float)ix + .5f);

		if (y > iy + .5f) {
			/* NW corner */
			y_t = y - ((float)iy + .5f);

			corners[0][0] = ctxt->cells[i];
			corners[0][1] = ctxt->cells[i + 1];
			corners[1][0] = ctxt->cells[i + GRID_SIZE];
			corners[1][1] = ctxt->cells[i + GRID_SIZE + 1];
		} else {
			/* SW corner */
			y_t = 1.f - (((float)iy + .5f) - y);

			corners[1][0] = ctxt->cells[i];
			corners[1][1] = ctxt->cells[i + 1];
			corners[0][0] = ctxt->cells[i - GRID_SIZE];
			corners[0][1] = ctxt->cells[i - GRID_SIZE + 1];
		}
	} else {
		x_t = 1.f - (((float)ix + .5f) - x);

		if (y > iy + .5f) {
			/* NE corner */
			y_t = y - ((float)iy + .5f);

			corners[0][1] = ctxt->cells[i];
			corners[0][0] = ctxt->cells[i - 1];
			corners[1][1] = ctxt->cells[i + GRID_SIZE];
			corners[1][0] = ctxt->cells[i + GRID_SIZE - 1];
		} else {
			/* SE corner */
			y_t = 1.f - (((float)iy + .5f) - y);

			corners[1][1] = ctxt->cells[i];
			corners[1][0] = ctxt->cells[i - 1];
			corners[0][1] = ctxt->cells[i - GRID_SIZE];
			corners[0][0] = ctxt->cells[i - GRID_SIZE - 1];
		}
	}

	/* short-circuit cases where interpolation obviously wouldn't do anything */
	if (corners[0][0] == corners[0][1] &&
	    corners[0][1] == corners[1][1] &&
	    corners[1][1] == corners[1][0])
		return color_to_uint32(colors[corners[0][0]]);

	x_t = smootherstep(0.f, 1.f, x_t);
	y_t = smootherstep(0.f, 1.f, y_t);

	x1 = color_lerp(&colors[corners[0][0]], &colors[corners[0][1]], x_t);
	x2 = color_lerp(&colors[corners[1][0]], &colors[corners[1][1]], x_t);

	return color_to_uint32(color_lerp(&x1, &x2, y_t));
}


static inline uint32_t sample_grid(submit_context_t *ctxt, float x, float y)
{
	return color_to_uint32(colors[ctxt->cells[(int)y * GRID_SIZE + (int)x]]);
}


static void draw_grid(submit_context_t *ctxt, til_fb_fragment_t *fragment)
{
	float	xscale = ((float)GRID_SIZE - 1.f) / (float)fragment->frame_width;
	float	yscale = ((float)GRID_SIZE - 1.f) / (float)fragment->frame_height;

	if (!fragment->cleared) {
		for (int y = 0; y < fragment->height; y++) {
			for (int x = 0; x < fragment->width; x++) {
				uint32_t	color;

				/* TODO: this could be optimized a bit! i.e. don't recompute the y for every x etc. */
				color = sample_grid(ctxt, .5f + ((float)(fragment->x + x)) * xscale, .5f + ((float)(fragment->y + y)) * yscale);
				til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, color);
			}
		}
	} else {
		for (int y = 0; y < fragment->height; y++) {
			for (int x = 0; x < fragment->width; x++) {
				uint32_t	color;

				/* TODO: this could be optimized a bit! i.e. don't recompute the y for every x etc. */
				color = sample_grid(ctxt, .5f + ((float)(fragment->x + x)) * xscale, .5f + ((float)(fragment->y + y)) * yscale);
				if ((color & 0xff000000) == 0xff000000)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, color);
			}
		}

	}
}


static void draw_grid_bilerp(submit_context_t *ctxt, til_fb_fragment_t *fragment)
{
	float	xscale = ((float)GRID_SIZE - 2.f) / (float)fragment->frame_width;
	float	yscale = ((float)GRID_SIZE - 2.f) / (float)fragment->frame_height;

	if (!fragment->cleared) {
		for (int y = 0; y < fragment->height; y++) {
			for (int x = 0; x < fragment->width; x++) {
				uint32_t	color;

				/* TODO: this could be optimized a bit! i.e. don't recompute the y for every x etc. */
				color = sample_grid_bilerp(ctxt, 1.f + ((float)(fragment->x + x)) * xscale, 1.f + ((float)(fragment->y + y)) * yscale);
				til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, color);
			}
		}
	} else {
		for (int y = 0; y < fragment->height; y++) {
			for (int x = 0; x < fragment->width; x++) {
				uint32_t	color;

				/* TODO: this could be optimized a bit! i.e. don't recompute the y for every x etc. */
				color = sample_grid_bilerp(ctxt, 1.f + ((float)(fragment->x + x)) * xscale, 1.f + ((float)(fragment->y + y)) * yscale);
				if ((color & 0xff000000) == 0xff000000)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, color);
			}
		}
	}
}


static void taken(void *ctx, uint32_t x, uint32_t y, uint32_t player)
{
	submit_context_t	*c = ctx;

	c->cells[y * GRID_SIZE + x] = player;
}


static void won(void *ctx, uint32_t player)
{
	submit_context_t	*c = ctx;

	c->game_winner = player;
}


static grid_ops_t submit_ops = {
	.taken = taken,
	.won = won,
};


static void setup_grid(submit_context_t *ctxt)
{
	grid_ops_t	*ops = &submit_ops;

	if (ctxt->grid)
		grid_free(ctxt->grid);

	ctxt->grid = grid_new(NUM_PLAYERS, GRID_SIZE, GRID_SIZE);
	for (int i = 0; i < NUM_PLAYERS; i++, ops = NULL)
		ctxt->players[i] = grid_player_new(ctxt->grid, ops, ctxt);

	memset(ctxt->cells, 0, sizeof(ctxt->cells));

	/* this makes the transition between games less visually jarring */
	colors[0] = colors[ctxt->game_winner];

	ctxt->game_winner = ctxt->seq = 0;
}


static til_module_context_t * submit_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	submit_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(submit_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->bilerp = ((submit_setup_t *)setup)->bilerp;
	setup_grid(ctxt);

	return &ctxt->til_module_context;
}


static void submit_destroy_context(til_module_context_t *context)
{
	submit_context_t	*ctxt = (submit_context_t *)context;

	grid_free(ctxt->grid);
	free(ctxt);
}


static void submit_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	submit_context_t	*ctxt = (submit_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_tile64 };

	if (ticks == context->last_ticks)
		return;

	if (ctxt->game_winner)
		setup_grid(ctxt);

	for (int i = 0; i < NUM_PLAYERS; i++) {
		int	moves = rand_r(&ctxt->til_module_context.seed) % TICKS_PER_FRAME;

		for (int j = 0; j < moves; j++)
			grid_player_plan(ctxt->players[i], ctxt->seq++,
				rand_r(&ctxt->til_module_context.seed) % GRID_SIZE,
				rand_r(&ctxt->til_module_context.seed) % GRID_SIZE);
	}

	for (int j = 0; j < TICKS_PER_FRAME; j++)
		grid_tick(ctxt->grid);
}


static void submit_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	submit_context_t	*ctxt = (submit_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	if (!ctxt->bilerp)
		draw_grid(ctxt, fragment);
	else
		draw_grid_bilerp(ctxt, fragment);
}


static int submit_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	submit_module = {
	.create_context = submit_create_context,
	.destroy_context = submit_destroy_context,
	.prepare_frame = submit_prepare_frame,
	.render_fragment = submit_render_fragment,
	.setup = submit_setup,
	.name = "submit",
	.description = "Cellular automata conquest game sim (threaded (poorly))",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


static int submit_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*values[] = {
				"off",
				"on",
				NULL
			};
	til_setting_t	*bilerp;
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Bilinearly interpolate cell colors",
							.key = "bilerp",
							.regex = NULL,
							.preferred = values[0],
							.values = values,
							.annotations = NULL
						},
						&bilerp,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		submit_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &submit_module);
		if (!setup)
			return -ENOMEM;

		if (!strcasecmp(bilerp->value, "on"))
			setup->bilerp = 1;

		*res_setup = &setup->til_setup;
	}

	return 0;
}
