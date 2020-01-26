/*
 *  Copyright (C) 2018 - Vito Caputo - <vcaputo@pengaru.com>
 *
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 3 as published
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

#include "fb.h"
#include "rototiller.h"
#include "settings.h"
#include "util.h"

#include "grid/grid.h"

#define NUM_PLAYERS	8
#define GRID_SIZE	60
#define TICKS_PER_FRAME 8000

typedef struct color_t {
	float	r, g, b;
} color_t;

static color_t colors[NUM_PLAYERS + 1] = {
	{},		 	/* uninitialized cell starts black, becomes winner colors */
	{1.f, .317f, 0.f },	/* orange */
	{.627f, .125f, 1.f },	/* blue */
	{.878f, 0.f, 0.f },	/* red */
	{.165f, .843f, .149f },	/* green */
	{0.f, .878f, .815f },	/* cyan */
	{.878f, 0.f, 1.f },	/* purple */
	{.906f, .937f, 0.f }, 	/* yellow */
	{}, 			/* black */
};


typedef struct submit_context_t {
	grid_t		*grid;
	grid_player_t	*players[NUM_PLAYERS];
	uint32_t	seq;
	uint32_t	game_winner;
	uint8_t		cells[GRID_SIZE * GRID_SIZE];
} submit_context_t;


static int	bilerp_setting;


/* convert a color into a packed, 32-bit rgb pixel value (taken from libs/ray/ray_color.h) */
static inline uint32_t color_to_uint32(color_t color) {
	uint32_t	pixel;

	if (color.r > 1.0f) color.r = 1.0f;
	if (color.g > 1.0f) color.g = 1.0f;
	if (color.b > 1.0f) color.b = 1.0f;

	pixel = (uint32_t)(color.r * 255.0f);
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


static void draw_grid(submit_context_t *ctxt, fb_fragment_t *fragment)
{
	float	xscale = ((float)GRID_SIZE - 1.f) / (float)fragment->frame_width;
	float	yscale = ((float)GRID_SIZE - 1.f) / (float)fragment->frame_height;

	for (int y = 0; y < fragment->height; y++) {
		for (int x = 0; x < fragment->width; x++) {
			uint32_t	color;

			/* TODO: this could be optimized a bit! i.e. don't recompute the y for every x etc. */
			color = sample_grid(ctxt, .5f + ((float)(fragment->x + x)) * xscale, .5f + ((float)(fragment->y + y)) * yscale);
			fb_fragment_put_pixel_unchecked(fragment, fragment->x + x, fragment->y + y, color);
		}
	}
}


static void draw_grid_bilerp(submit_context_t *ctxt, fb_fragment_t *fragment)
{
	float	xscale = ((float)GRID_SIZE - 1.f) / (float)fragment->frame_width;
	float	yscale = ((float)GRID_SIZE - 1.f) / (float)fragment->frame_height;

	for (int y = 0; y < fragment->height; y++) {
		for (int x = 0; x < fragment->width; x++) {
			uint32_t	color;

			/* TODO: this could be optimized a bit! i.e. don't recompute the y for every x etc. */
			color = sample_grid_bilerp(ctxt, .5f + ((float)(fragment->x + x)) * xscale, .5f + ((float)(fragment->y + y)) * yscale);
			fb_fragment_put_pixel_unchecked(fragment, fragment->x + x, fragment->y + y, color);
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


static void * submit_create_context(unsigned ticks, unsigned num_cpus)
{
	submit_context_t	*ctxt;

	ctxt = calloc(1, sizeof(submit_context_t));
	if (!ctxt)
		return NULL;

	setup_grid(ctxt);

	return ctxt;
}


static void submit_destroy_context(void *context)
{
	submit_context_t	*ctxt = context;

	grid_free(ctxt->grid);
	free(ctxt);
}


static int submit_fragmenter(void *context, const fb_fragment_t *fragment, unsigned number, fb_fragment_t *res_fragment)
{
	return fb_fragment_tile_single(fragment, 32, number, res_fragment);
}


static void submit_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter)
{
	submit_context_t	*ctxt = context;

	*res_fragmenter = submit_fragmenter;

	if (ctxt->game_winner)
		setup_grid(ctxt);

	for (int i = 0; i < NUM_PLAYERS; i++) {
		int	moves = rand() % TICKS_PER_FRAME;

		for (int j = 0; j < moves; j++)
			grid_player_plan(ctxt->players[i], ctxt->seq++, rand() % GRID_SIZE, rand() % GRID_SIZE);
	}

	for (int j = 0; j < TICKS_PER_FRAME; j++)
		grid_tick(ctxt->grid);
}


static void submit_render_fragment(void *context, unsigned ticks, unsigned cpu, fb_fragment_t *fragment)
{
	submit_context_t	*ctxt = context;

	if (!bilerp_setting)
		draw_grid(ctxt, fragment);
	else
		draw_grid_bilerp(ctxt, fragment);
}


static int submit_setup(const settings_t *settings, setting_desc_t **next_setting)
{
	const char	*bilerp;

	bilerp = settings_get_value(settings, "bilerp");
	if (!bilerp) {
		const char	*values[] = {
					"off",
					"on",
					NULL
				};
		int		r;

		r = setting_desc_clone(&(setting_desc_t){
						.name = "Bilinear Interpolation of Cell Colors",
						.key = "bilerp",
						.regex = NULL,
						.preferred = values[0],
						.values = values,
						.annotations = NULL
					}, next_setting);
		if (r < 0)
			return r;

		return 1;
	}

	if (!strcasecmp(bilerp, "on"))
		bilerp_setting = 1;
	else
		bilerp_setting = 0;

	return 0;
}


rototiller_module_t	submit_module = {
	.create_context = submit_create_context,
	.destroy_context = submit_destroy_context,
	.prepare_frame = submit_prepare_frame,
	.render_fragment = submit_render_fragment,
	.name = "submit",
	.description = "Cellular automata conquest game sim (threaded (poorly))",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv3",
	.setup = submit_setup,
};
