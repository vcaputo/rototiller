/*
 *  Copyright (C) 2020 - Vito Caputo - <vcaputo@pengaru.com>
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

#ifndef _PUDDLE_H
#define _PUDDLE_H

typedef struct puddle_t puddle_t;
typedef struct v2f_t v2f_t;

puddle_t * puddle_new(int w, int h);
void puddle_free(puddle_t *puddle);
void puddle_tick(puddle_t *puddle, float viscosity);
void puddle_set(puddle_t *puddle, int x, int y, float v);
float puddle_sample(const puddle_t *puddle, const v2f_t *coordinate);


#endif
