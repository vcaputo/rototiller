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

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "puddle.h"

typedef struct puddle_t {
	int	w, h;
	float	*a, *b;
	float	floats[];
} puddle_t;

typedef struct v2f_t {
	float	x, y;
} v2f_t;


puddle_t * puddle_new(int w, int h)
{
	puddle_t	*puddle;

	puddle = calloc(1, sizeof(puddle_t) + sizeof(float) * w * (h + 2) * 2);
	if (!puddle)
		return NULL;

	puddle->w = w;
	puddle->h = h;

	puddle->a = &puddle->floats[w];
	puddle->b = &puddle->floats[w * 2 + w * h + w];

	return puddle;
}


void puddle_free(puddle_t *puddle)
{
	free(puddle);
}


/* Run the puddle simulation for a tick, using the supplied viscosity value.
 * A good viscosity value is ~.01, YMMV.
 */
void puddle_tick(puddle_t *puddle, float viscosity)
{
	float	*a, *b;

	assert(puddle);

	a = puddle->a;
	b = puddle->b;

	for (int y = 0, i = 0; y < puddle->h; y++) {
		for (int x = 0; x < puddle->w; x++, i++) {
			float	tmp =	a[i - puddle->w] +
					a[i - 1] +
					a[i + 1] +
					a[i + puddle->w];

			tmp -= b[i] * 2.f;
			tmp *= .5f;
			tmp -= tmp * viscosity;

			b[i] = tmp;
		}
	}

	puddle->b = a;
	puddle->a = b;
}


/* Set a specific cell in the puddle to the supplied value */
void puddle_set(puddle_t *puddle, int x, int y, float value)
{
	assert(puddle);
	assert(x >= 0 && x < puddle->w);
	assert(y >= 0 && y < puddle->h);

	puddle->a[y * puddle->w + x] = value;
}


static inline float lerp(float a, float b, float t)
{
	return (1.0f - t) * a + t * b;
}


/* Sample the supplied puddle field at the specified coordinate.
 *
 * The puddle field is treated as a unit square mapped to the specified
 * dimensions @ create time.  the sampled value is linearly interpolated from
 * the data.
 */
float puddle_sample(const puddle_t *puddle, const v2f_t *coordinate)
{
	int	x0, y0, x1, y1;
	float	x, y, tx, ty;

	assert(puddle);
	assert(coordinate);

	x = .5f + (coordinate->x * .5f + .5f) * (puddle->w - 2);
	y = .5f + (coordinate->y * .5f + .5f) * (puddle->h - 2);

	x0 = floorf(x);
	y0 = floorf(y);

	x1 = x0 + 1;
	y1 = y0 + 1;

	tx = x - (float)x0;
	ty = y - (float)y0;

	return lerp(lerp(puddle->a[y0 * puddle->w + x0], puddle->a[y0 * puddle->w + x1], tx),
		    lerp(puddle->a[y1 * puddle->w + x0], puddle->a[y1 * puddle->w + x1], tx),
		    ty);
}
