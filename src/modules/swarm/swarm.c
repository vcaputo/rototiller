/*
 *  Copyright (C) 2021 - Vito Caputo - <vcaputo@pengaru.com>
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

/* this implements a very simplified "boids" inspired particles swarm
 * http://www.red3d.com/cwr/boids/
 * https://en.wikipedia.org/wiki/Boids
 * https://en.wikipedia.org/wiki/Swarm_intelligence
 */

#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "til.h"
#include "til_fb.h"

#define SWARM_SIZE	(32 * 1024)
#define SWARM_ZCONST	4.f

typedef struct v3f_t {
	float	x, y, z;
} v3f_t;

typedef struct v2f_t {
	float	x, y;
} v2f_t;

typedef struct boid_t {
	v3f_t	position;
	v3f_t	direction;
	float	velocity;
} boid_t;

typedef struct swarm_context_t {
	v3f_t		color;
	float		ztweak;
	boid_t		boids[];
} swarm_context_t;


static inline float randf(float min, float max)
{
	return ((float)rand() / (float)RAND_MAX) * (max - min) + min;
}


static inline void v3f_rand(v3f_t *v, float min, float max)
{
	v->x = randf(min, max);
	v->y = randf(min, max);
	v->z = randf(min, max);
}


static inline v3f_t v3f_add(v3f_t a, v3f_t b)
{
	return (v3f_t){
		.x = a.x + b.x,
		.y = a.y + b.y,
		.z = a.z + b.z,
	};
}


static inline v3f_t v3f_sub(v3f_t a, v3f_t b)
{
	return (v3f_t){
		.x = a.x - b.x,
		.y = a.y - b.y,
		.z = a.z - b.z,
	};
}


static inline v3f_t v3f_mult_scalar(v3f_t v, float scalar)
{
	return (v3f_t){
		.x = v.x * scalar,
		.y = v.y * scalar,
		.z = v.z * scalar,
	};
}


static inline float v3f_len(v3f_t v)
{
	return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}


static inline void v3f_normalize(v3f_t *v)
{
	float	l = 1.f / v3f_len(*v);

	v->x *= l;
	v->y *= l;
	v->z *= l;
}


static v3f_t v3f_lerp(v3f_t a, v3f_t b, float t)
{
	return (v3f_t){
		.x = (b.x - a.x) * t + a.x,
		.y = (b.y - a.y) * t + a.y,
		.z = (b.z - a.z) * t + a.z,
	};
}


static void boid_randomize(boid_t *boid)
{
	v3f_rand(&boid->position, -1.f, 1.f);
	v3f_rand(&boid->direction, -1.f, 1.f);
	v3f_normalize(&boid->direction);
	boid->velocity = randf(.05f, .2f);
}


/* convert a color into a packed, 32-bit rgb pixel value (taken from libs/ray/ray_color.h) */
static inline uint32_t color_to_uint32(v3f_t color) {
	uint32_t	pixel;

	if (color.x > 1.0f) color.x = 1.0f;
	if (color.y > 1.0f) color.y = 1.0f;
	if (color.z > 1.0f) color.z = 1.0f;

	if (color.x < .0f) color.x = .0f;
	if (color.y < .0f) color.y = .0f;
	if (color.z < .0f) color.z = .0f;

	pixel = (uint32_t)(color.x * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.y * 255.0f);
	pixel <<= 8;
	pixel |= (uint32_t)(color.z * 255.0f);

	return pixel;
}


static void * swarm_create_context(unsigned ticks, unsigned num_cpus)
{
	swarm_context_t	*ctxt;

	ctxt = calloc(1, sizeof(swarm_context_t) + sizeof(*(ctxt->boids)) * SWARM_SIZE);
	if (!ctxt)
		return NULL;

	for (unsigned i = 0; i < SWARM_SIZE; i++)
		boid_randomize(&ctxt->boids[i]);

	return ctxt;
}


static void swarm_destroy_context(void *context)
{
	swarm_context_t	*ctxt = context;

	free(ctxt);
}


static void swarm_update(swarm_context_t *ctxt, unsigned ticks)
{
	v3f_t	avg_direction = {};
	float	avg_velocity = 0.f;
	v3f_t	avg_center = {};
	float	wleader, wcenter, wdirection;

	{ /* [0] = leader */
		float	r = M_PI * 2 * ((cosf((float)ticks * .001f) * .5f + .5f));

		ctxt->boids[0].position.x = cosf(r);
		ctxt->boids[0].position.y = sinf(r);
		ctxt->boids[0].position.z = cosf(r * 2.f);
	}

	/* characterize the current swarm */
	for (unsigned i = 0; i < SWARM_SIZE; i++) {
		boid_t	*b = &ctxt->boids[i];

		avg_center = v3f_add(avg_center, b->position);
		avg_direction = v3f_add(avg_direction, b->direction);
		avg_velocity += b->velocity;
	}

	avg_velocity *= (1.f / (float)SWARM_SIZE);
	avg_center = v3f_mult_scalar(avg_center, (1.f / (float)SWARM_SIZE));
	avg_direction = v3f_mult_scalar(avg_direction, (1.f / (float)SWARM_SIZE));
	v3f_normalize(&avg_direction);

	/* vary weights */
	wleader = cosf((float)ticks * .001f) * .5f + .5f;
	wcenter = cosf((float)ticks * .0005f) * .5f + .5f;
	wdirection = sinf((float)ticks * .003f) * .5f + .5f;

	/* update the followers in relation to leader and swarm itself */
	for (unsigned i = 1; i < SWARM_SIZE; i++) {
		boid_t	*b = &ctxt->boids[i];
		v3f_t	to_leader = v3f_sub(ctxt->boids[0].position, b->position);
		v3f_t	to_center = v3f_sub(avg_center, b->position);

		v3f_normalize(&to_leader);
		b->direction = v3f_lerp(b->direction, to_leader, wleader * .1f);
		v3f_normalize(&b->direction);
		b->direction = v3f_lerp(b->direction, to_center, wcenter * .1f);
		v3f_normalize(&b->direction);
		b->direction = v3f_lerp(b->direction, avg_direction, wdirection * .05f);
		v3f_normalize(&b->direction);

		b->position = v3f_add(b->position, v3f_mult_scalar(b->direction, b->velocity));
	}

	/* color the swarm according to the current weights */
	ctxt->color.x = wleader;
	ctxt->color.y = wcenter;
	ctxt->color.z = wdirection;

	/* this zooms out a bit when the swarm loosens up, gauged by low weights */
	ctxt->ztweak = (1.8f - v3f_len(ctxt->color)) * 4.f;
}


static void swarm_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	swarm_context_t	*ctxt = context;

	swarm_update(ctxt, ticks);

	til_fb_fragment_zero(fragment);

	{
		float		fw = fragment->frame_width, fh = fragment->frame_height;
		uint32_t	color = color_to_uint32(ctxt->color);

		fw *= .5f;
		fh *= .5f;

		for (unsigned i = 0; i < SWARM_SIZE; i++) {
			boid_t	*b = &ctxt->boids[i];
			v2f_t	nc;

			nc.x = b->position.x / (b->position.z + SWARM_ZCONST + ctxt->ztweak);
			nc.y = b->position.y / (b->position.z + SWARM_ZCONST + ctxt->ztweak);

			nc.x = nc.x * fw + fw;
			nc.y = nc.y * fh + fh;

			til_fb_fragment_put_pixel_checked(fragment, nc.x, nc.y, color);
		}
	}
}


til_module_t	swarm_module = {
	.create_context = swarm_create_context,
	.destroy_context = swarm_destroy_context,
	.render_fragment = swarm_render_fragment,
	.name = "swarm",
	.description = "\"Boids\"-inspired particle swarm in 3D",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
