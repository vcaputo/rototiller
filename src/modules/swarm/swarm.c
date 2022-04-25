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

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "til.h"
#include "til_fb.h"

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

typedef enum swarm_draw_style_t {
	SWARM_DRAW_STYLE_POINTS,	/* simple opaque pixel per particle */
	SWARM_DRAW_STYLE_LINES,		/* simple opaque lines per particle, oriented and sized by direction and velocity */
} swarm_draw_style_t;

typedef struct swarm_setup_t {
	til_setup_t		til_setup;
	swarm_draw_style_t	draw_style;
} swarm_setup_t;

typedef struct swarm_context_t {
	v3f_t		color;
	float		ztweak;
	swarm_setup_t	setup;
	boid_t		boids[];
} swarm_context_t;

#define SWARM_SIZE		(32 * 1024)
#define SWARM_ZCONST		4.f
#define SWARM_DEFAULT_STYLE	SWARM_DRAW_STYLE_LINES

static swarm_setup_t swarm_default_setup = {
	.draw_style = SWARM_DEFAULT_STYLE,
};


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


static inline v3f_t v3f_invert(v3f_t v)
{
	return (v3f_t){
		.x = -v.x,
		.y = -v.y,
		.z = -v.z,
	};
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


static void * swarm_create_context(unsigned ticks, unsigned num_cpus, til_setup_t *setup)
{
	swarm_context_t	*ctxt;

	if (!setup)
		setup = &swarm_default_setup.til_setup;

	ctxt = calloc(1, sizeof(swarm_context_t) + sizeof(*(ctxt->boids)) * SWARM_SIZE);
	if (!ctxt)
		return NULL;

	ctxt->setup = *(swarm_setup_t *)setup;

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
		v3f_t	newpos = {
				.x = cosf(r),
				.y = sinf(r),
				.z = cosf(r * 2.f),
			};
		boid_t	*b = &ctxt->boids[0];

		if (newpos.x != b->position.x ||
		    newpos.y != b->position.y ||
		    newpos.z != b->position.z) {

			/* XXX: this must be conditional on position changing otherwise
			 * it could produce a zero direction vector, making normalize
			 * spit out NaN, and things fall apart.
			 */

			b->direction = v3f_sub(b->position, newpos);
			b->velocity = v3f_len(b->direction);
			v3f_normalize(&b->direction);
			b->position = newpos;

		}
#if 0
		printf("pos={%f,%f,%f},dir={%f,%f,%f},v=%f\n",
			b->position.x, b->position.y, b->position.z,
			b->direction.x, b->direction.y, b->direction.z,
			b->velocity);
#endif
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


static inline v2f_t swarm_project_point(swarm_context_t *ctxt, v3f_t *point)
{
	return (v2f_t) {
		.x = point->x / (point->z + SWARM_ZCONST + ctxt->ztweak),
		.y = point->y / (point->z + SWARM_ZCONST + ctxt->ztweak),
	};
}


static inline v2f_t swarm_scale(v2f_t normcoord, v2f_t scale)
{
	return (v2f_t) {
		.x = normcoord.x * scale.x + scale.x,
		.y = normcoord.y * scale.y + scale.y,
	};
}


static inline v2f_t swarm_clip(v2f_t coord, til_fb_fragment_t *fragment)
{
//	printf("coord={%f,%f}\n", coord.x, coord.y);

	return (v2f_t) {
		.x = coord.x < 0.f ? 0.f : (coord.x > (fragment->frame_width - 1) ? (fragment->frame_width - 1) : coord.x),
		.y = coord.y < 0.f ? 0.f : (coord.y > (fragment->frame_height - 1) ? (fragment->frame_height - 1) : coord.y),
	};
}


static void swarm_draw_as_points(swarm_context_t *ctxt, til_fb_fragment_t *fragment)
{
	v2f_t		scale = (v2f_t){
				.x = fragment->frame_width * .5f,
				.y = fragment->frame_height * .5f,
			};
	uint32_t	color = color_to_uint32(ctxt->color);

	for (unsigned i = 0; i < SWARM_SIZE; i++) {
		boid_t	*b = &ctxt->boids[i];
		v2f_t	nc = swarm_scale(swarm_project_point(ctxt, &b->position), scale);

		til_fb_fragment_put_pixel_checked(fragment, nc.x, nc.y, color);
	}
}


/* though this is called _unchecked(), it's temporarily _checked() due to random segfaults. */
static void draw_line_unchecked(til_fb_fragment_t *fragment, int x1, int y1, int x2, int y2, uint32_t color)
{
	int	x_delta = x2 - x1;
	int	y_delta = y2 - y1;
	int	sdx = x_delta < 0 ? -1 : 1;
	int	sdy = y_delta < 0 ? -1 : 1;

	x_delta = abs(x_delta);
	y_delta = abs(y_delta);

	if (x_delta >= y_delta) {
		/* X-major */
		for (int minor = 0, x = 0; x <= x_delta; x++, x1 += sdx, minor += y_delta) {
			if (minor >= x_delta) {
				y1 += sdy;
				minor -= x_delta;
			}
			/* XXX FIXME: segfaults occasionally when _unchecked !!! */
			til_fb_fragment_put_pixel_checked(fragment, x1, y1, color);
		}
	} else {
		/* Y-major */
		for (int minor = 0, y = 0; y <= y_delta; y++, y1 += sdy, minor += x_delta) {
			if (minor >= y_delta) {
				x1 += sdx;
				minor -= y_delta;
			}

			/* XXX FIXME: segfaults occasionally when _unchecked !!! */
			til_fb_fragment_put_pixel_checked(fragment, x1, y1, color);
		}
	}
}


static void swarm_draw_as_lines(swarm_context_t *ctxt, til_fb_fragment_t *fragment)
{
	v2f_t		scale = (v2f_t){
				.x = fragment->frame_width * .5f,
				.y = fragment->frame_height * .5f,
			};
	uint32_t	color = color_to_uint32(ctxt->color);

	/* this is similar to draw_as_points(), but derives two 3D points per boid,
	 * connecting them with a line in 2D.
	 */
	for (unsigned i = 0; i < SWARM_SIZE; i++) {
		boid_t	*b = &ctxt->boids[i];
		v3f_t	p1, p2;
		v2f_t	nc1, nc2;

		p1 = v3f_add(b->position, v3f_mult_scalar(b->direction, b->velocity));
		p2 = v3f_add(b->position, v3f_mult_scalar(v3f_invert(b->direction), b->velocity));

		/* don't bother drawing anything too close/behind the viewer, it
		 * just produces diagonal lines across the entire frame.
		 */
		if (p1.z < -SWARM_ZCONST && p2.z < -SWARM_ZCONST)
			continue;

		nc1 = swarm_clip(swarm_scale(swarm_project_point(ctxt, &p1), scale), fragment);
		nc2 = swarm_clip(swarm_scale(swarm_project_point(ctxt, &p2), scale), fragment);

		draw_line_unchecked(fragment, nc1.x, nc1.y, nc2.x, nc2.y, color);
	}
}


static void swarm_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	swarm_context_t	*ctxt = context;

	swarm_update(ctxt, ticks);

	til_fb_fragment_clear(fragment);

	switch (ctxt->setup.draw_style) {
	case SWARM_DRAW_STYLE_POINTS:
		return swarm_draw_as_points(ctxt, fragment);
	case SWARM_DRAW_STYLE_LINES:
		return swarm_draw_as_lines(ctxt, fragment);
	}
}


static int swarm_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*styles[] = {
				"points",
				"lines",
				NULL,
			};
	const char	*style;
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Particle drawing style",
							.key = "style",
							.values = styles,
							.preferred = styles[SWARM_DEFAULT_STYLE],
							.annotations = NULL
						},
						&style,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		swarm_setup_t	*setup;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		for (int i = 0; styles[i]; i++) {
			if (!strcmp(styles[i], style))
				setup->draw_style = i;
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	swarm_module = {
	.create_context = swarm_create_context,
	.destroy_context = swarm_destroy_context,
	.render_fragment = swarm_render_fragment,
	.setup = swarm_setup,
	.name = "swarm",
	.description = "\"Boids\"-inspired particle swarm in 3D",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
