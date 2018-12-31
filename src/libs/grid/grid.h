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

#ifndef _GRID_H
#define _GRID_H

#include <stdint.h>

typedef enum grid_ops_move_result_t {
	GRID_OPS_MOVE_RESULT_FAIL,
	GRID_OPS_MOVE_RESULT_SUCCESS,
	GRID_OPS_MOVE_RESULT_NOOP,
} grid_ops_move_result_t;

/* hooks to integrate from back-end to front-end */
typedef struct grid_ops_t {
	void	(*setup)(void *ctx, uint32_t player);				/* the specified player number has been assigned to this context */
	void	(*shutdown)(void *ctx);						/* the grid has shutdown */
	void	(*joined)(void *ctx, uint32_t player);				/* the specified player joined */
	void	(*parted)(void *ctx, uint32_t player);				/* the specified player parted */
	void	(*said)(void *ctx, uint32_t player, const char *text);		/* the specified player says text */
	void	(*planned)(void *ctx, uint32_t move);				/* the specified move has been planned */
	void	(*executed)(void *ctx, uint32_t move, grid_ops_move_result_t result);/* the specified move has been executed, removed from plan */
	void	(*canceled)(void *ctx, uint32_t move);				/* the specified move has been canceled, removed from plan */
	void	(*taken)(void *ctx, uint32_t x, uint32_t y, unsigned player);	/* the specified cell has been taken by the specified player */
	void	(*won)(void *ctx, uint32_t player);				/* the game has been won by the specified player */
} grid_ops_t;

typedef struct grid_t grid_t;
typedef struct grid_player_t grid_player_t;

grid_t * grid_new(uint32_t players, uint32_t width, uint32_t height);
void grid_free(grid_t *grid);
void grid_tick(grid_t *grid);

grid_player_t * grid_player_new(grid_t *grid, const grid_ops_t *ops, void *ops_ctx);
void grid_player_free(grid_player_t *player);
void grid_player_plan(grid_player_t *player, uint32_t move, uint32_t x, uint32_t y);
void grid_player_cancel(grid_player_t *player, uint32_t move);
void grid_player_say(grid_player_t *player, const char *text);

#endif
