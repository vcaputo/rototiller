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

/* This implements a simple cellular automata engine with basic rules, taken
 * from a multiplayer game project I'm working on hence the concept of players,
 * variable move planning queues, and a rudimentary chat function.  It's all
 * pluggable for mixed use as a client-resident local component, or as a daemon
 * for remote multiplayer games, and it appears, simple use as a gadget in
 * programs like rototiller.
 *
 * The rules are currently fixed in execute_plan(), but could easily be made
 * pluggable by having the execute_plan() supplied to grid_new(). TODO
 *
 * Note this is also rather allocation/free heavy, this hasn't seen any
 * optimization at all.
 */

#include <assert.h>
#include <stdlib.h>

#include "grid.h"
#include "macros.h"

typedef struct grid_plan_t grid_plan_t;
typedef struct grid_player_t grid_player_t;
typedef struct grid_t grid_t;

struct grid_plan_t {
	grid_plan_t		*next, *prev;
	uint32_t		x, y;
	uint32_t		id;
};

struct grid_player_t {
	grid_player_t		*next, *prev;
	grid_t			*grid;
	const grid_ops_t	*ops;
	void			*ops_ctx;

	grid_plan_t		*plan_tail, *plan_head;
	uint32_t		n_cells;
	uint32_t		id;
};

struct grid_t {
	grid_player_t		*players;
	uint32_t		req_players, num_players;
	uint32_t		next_player;
	uint32_t		width, height;
	uint32_t		cells[];
};


grid_t * grid_new(uint32_t players, uint32_t width, uint32_t height)
{
	grid_t	*grid;

	assert(players && width && height);

	grid = calloc(1, sizeof(grid_t) + sizeof(uint32_t) * width * height);
	fatal_if(!grid, "Unable to allocate grid_t");

	grid->width = width;
	grid->height = height;
	grid->req_players = players;
	grid->next_player = 1;	/* XXX: zero is reserved for blank cells */

	return grid;
}


void grid_free(grid_t *grid)
{
	grid_player_t	*p, *p_next;

	assert(grid);

	for (p = grid->players; p != NULL; p = p_next) {
		p_next = p->next;

		if (p->ops->shutdown)
			p->ops->shutdown(p->ops_ctx);

		free(p);
	}

	free(grid);
}


static grid_ops_move_result_t execute_plan(grid_player_t *player, grid_plan_t *plan)
{
	uint32_t	*cells, width, height;

	assert(player);
	assert(plan);

	cells = player->grid->cells;
	width = player->grid->width;
	height = player->grid->height;

	if (cells[plan->y * width + plan->x] == player->id)
		return GRID_OPS_MOVE_RESULT_NOOP;

	/* is the cell uncontested and orthogonally adjacent? */
	if (!cells[plan->y * width + plan->x]) {
		/* if the player has no cells, it may take any
		 * uncontested one.
		 */
		if (!player->n_cells)
			return GRID_OPS_MOVE_RESULT_SUCCESS;

		if (plan->x > 0 &&
		    cells[plan->y * width + plan->x - 1] == player->id)
			return GRID_OPS_MOVE_RESULT_SUCCESS;

		if (plan->x < width - 1 &&
		    cells[plan->y * width + plan->x + 1] == player->id)
			return GRID_OPS_MOVE_RESULT_SUCCESS;

		if (plan->y > 0 &&
		    cells[(plan->y - 1) * width + plan->x] == player->id)
			return GRID_OPS_MOVE_RESULT_SUCCESS;

		if (plan->y < height - 1 &&
		    cells[(plan->y + 1) * width + plan->x] == player->id)
			return GRID_OPS_MOVE_RESULT_SUCCESS;
	}

	/* does the player have two cells chained orthogonally adjacent? */
	if (plan->x > 1 &&
	    cells[plan->y * width + plan->x - 1] == player->id &&
	    cells[plan->y * width + plan->x - 2] == player->id)
		return GRID_OPS_MOVE_RESULT_SUCCESS;

	if (plan->x < width - 2 &&
	    cells[plan->y * width + plan->x + 1] == player->id &&
	    cells[plan->y * width + plan->x + 2] == player->id)
		return GRID_OPS_MOVE_RESULT_SUCCESS;

	if (plan->y > 1 &&
	    cells[(plan->y - 1) * width + plan->x] == player->id &&
	    cells[(plan->y - 2) * width + plan->x] == player->id)
		return GRID_OPS_MOVE_RESULT_SUCCESS;

	if (plan->y < height - 2 &&
	    cells[(plan->y + 1) * width + plan->x] == player->id &&
	    cells[(plan->y + 2) * width + plan->x] == player->id)
		return GRID_OPS_MOVE_RESULT_SUCCESS;

	/* does the player surround the target's group? */
	/* TODO TODO */

	return GRID_OPS_MOVE_RESULT_FAIL;
}


/* call this at the frequency desired for the game. */
void grid_tick(grid_t *grid)
{
	assert(grid);

	/* TODO: shuffle grid->players every tick */

	/* execute every player's next planned move */
	for (grid_player_t *p = grid->players; p != NULL; p = p->next) {
		grid_plan_t		*plan;
		grid_ops_move_result_t	res;

		plan = p->plan_head;
		if (!plan)
			continue;

		/* TODO: add a simple doubly linked list header, use it */
		p->plan_head = plan->next;
		if (p->plan_head)
			p->plan_head->prev = NULL;
		else
			p->plan_tail = NULL;

		res = execute_plan(p, plan);
		if (p->ops->executed)
			p->ops->executed(p->ops_ctx, plan->id, res);
		if (res == GRID_OPS_MOVE_RESULT_SUCCESS) {
			/* find the current owner, dec their n_cells */
			if (grid->cells[plan->y * grid->width + plan->x]) {
				for (grid_player_t *pp = grid->players; pp != NULL; pp = pp->next) {
					if (grid->cells[plan->y * grid->width + plan->x] == pp->id) {
						pp->n_cells--;
						break;
					}
				}
			}

			/* new ownership */
			grid->cells[plan->y * grid->width + plan->x] = p->id;
			p->n_cells++;

			/* notify all players of a successfully executed plan */
			for (grid_player_t *pp = grid->players; pp != NULL; pp = pp->next) {
				if (pp->ops->taken)
					pp->ops->taken(pp->ops_ctx, plan->x, plan->y, p->id);
			}

			/* winner! */
			if (p->n_cells == grid->width * grid->height) {
				for (grid_player_t *pp = grid->players; pp != NULL; pp = pp->next) {
					if (pp->ops->won)
						pp->ops->won(pp->ops_ctx, p->id);
				}
			}
		}

		free(plan);
	}
}


/* establish a new player on the specified grid, using the supplied ops to
 * communicate state changes from the grid back to the player.
 */
grid_player_t * grid_player_new(grid_t *grid, const grid_ops_t *ops, void *ops_ctx)
{
	static grid_ops_t	null_ops;
	grid_player_t		*player;

	assert(grid);

	if (!ops)
		ops = &null_ops;

	player = calloc(1, sizeof(grid_player_t));
	fatal_if(!player, "Unable to allocate grid_player_t");

	/* TODO: refuse when exceeding grid->req_players? */

	player->grid = grid;
	player->ops = ops;
	player->ops_ctx = ops_ctx;
	player->id = grid->next_player++;

	if (ops->setup)
		ops->setup(ops_ctx, player->id);

	/* TODO: add a simple doubly linked list header, use it */
	player->next = grid->players;
	if (player->next)
		player->next->prev = player;
	grid->players = player;

	grid->num_players++;

	for (grid_player_t *p = grid->players; p != NULL; p = p->next) {
		if (p->ops->joined)
			p->ops->joined(p->ops_ctx, player->id);
	}

	return player;
}


void grid_player_free(grid_player_t *player)
{
	assert(player);

	/* TODO: add a simple doubly linked list header, use it */
	if (player->next)
		player->next->prev = player->prev;

	if (player->prev)
		player->prev->next = player->next;
	else
		player->grid->players = player->next;

	player->grid->num_players--;

	for (grid_player_t *p = player->grid->players; p != NULL; p = p->next) {
		if (p->ops->parted)
			p->ops->parted(p->ops_ctx, player->id);
	}

	free(player);
}


void grid_player_plan(grid_player_t *player, uint32_t move, uint32_t x, uint32_t y)
{
	grid_plan_t	*plan;

	assert(player);
	assert(x < player->grid->width && y < player->grid->height);

	plan = calloc(1, sizeof(grid_plan_t));
	fatal_if(!plan, "Unable to allocate plan");

	plan->id = move;
	plan->x = x;
	plan->y = y;

	/* TODO: add a simple doubly linked list header, use it */
	plan->prev = player->plan_tail;
	player->plan_tail = plan;
	if (!plan->prev)
		player->plan_head = plan;
	else
		plan->prev->next = plan;

	if (player->ops->planned)
		player->ops->planned(player->ops_ctx, move);
}


void grid_player_cancel(grid_player_t *player, uint32_t move)
{
	grid_plan_t	*p;

	assert(player);

	for (p = player->plan_head; p != NULL; p = p->next)
		if (p->id == move)
			break;

	if (!p)
		return;

	/* TODO: add a simple doubly linked list header, use it */
	if (p->next)
		p->next->prev = p->prev;
	else
		player->plan_tail = p->prev;

	if (p->prev)
		p->prev->next = p->next;
	else
		player->plan_head = p->next;

	free(p);

	if (player->ops->canceled)
		player->ops->canceled(player->ops_ctx, move);
}


void grid_player_say(grid_player_t *player, const char *text)
{
	assert(player);
	assert(player->grid);

	for (grid_player_t *p = player->grid->players; p != NULL; p = p->next) {
		if (player->ops->said)
			p->ops->said(p->ops_ctx, player->id, text);
	}
}
