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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "fb.h"
#include "rototiller.h"
#include "util.h"

#include "grid/grid.h"

#define NUM_PLAYERS	8
#define GRID_SIZE	60
#define TICKS_PER_FRAME 8000

static uint32_t	colors[NUM_PLAYERS + 1] = {
	0x00000000,	/* uninitialized cell starts black, becomes winner colors */
	0xffff5000,	/* orange */
	0xff1020ff,	/* blue */
	0xffe00000,	/* red */
	0xff2ad726,	/* green */
	0xff00e0d0,	/* cyan */
	0xffd000ff,	/* purple */
	0xffe7ef00,	/* yellow */
	0x00000000,	/* black */
};

typedef struct submit_context_t {
	grid_t		*grid;
	grid_player_t	*players[NUM_PLAYERS];
	uint32_t	seq;
	uint32_t	game_winner;
	fb_fragment_t	*fragment;
	uint32_t	cells[GRID_SIZE * GRID_SIZE];
} submit_context_t;


/* TODO: drawing is not optimized at all */
static void draw_cell(fb_fragment_t *fragment, int x, int y, int w, int h, uint32_t color)
{
	for (int yy = 0; yy < h; yy++)
		for (int xx = 0; xx < w; xx++)
			fb_fragment_put_pixel_checked(fragment, x + xx, y + yy, color);
}


static void draw_grid(submit_context_t *ctxt, fb_fragment_t *fragment)
{
	int	w = fragment->width / GRID_SIZE;
	int	h = fragment->height / GRID_SIZE;
	int	xoff = (fragment->width - w * GRID_SIZE) / 2;
	int	yoff = (fragment->height - h * GRID_SIZE) / 2;

	for (int y = 0; y < GRID_SIZE; y++)
		for (int x = 0; x < GRID_SIZE; x++)
			draw_cell(fragment, xoff + x * w, yoff + y * h, w, h, colors[ctxt->cells[y * GRID_SIZE + x]]);
}


static void taken(void *ctx, uint32_t x, uint32_t y, unsigned player)
{
	submit_context_t	*c = ctx;

	assert(player);

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


static void * submit_create_context(void)
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


static void submit_render_fragment(void *context, fb_fragment_t *fragment)
{
	submit_context_t	*ctxt = context;

	ctxt->fragment = fragment;

	if (ctxt->game_winner)
		setup_grid(ctxt);

	for (int i = 0; i < NUM_PLAYERS; i++) {
		int	moves = rand() % TICKS_PER_FRAME;

		for (int j = 0; j < moves; j++)
			grid_player_plan(ctxt->players[i], ctxt->seq++, rand() % GRID_SIZE, rand() % GRID_SIZE);
	}

	for (int j = 0; j < TICKS_PER_FRAME; j++)
		grid_tick(ctxt->grid);

	draw_grid(ctxt, fragment);
}


rototiller_module_t	submit_module = {
	.create_context = submit_create_context,
	.destroy_context = submit_destroy_context,
	.render_fragment = submit_render_fragment,
	.name = "submit",
	.description = "Cellular automata conquest game sim",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv3",
};
