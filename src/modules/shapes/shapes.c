/*
 *  Copyright (C) 2022 - Vito Caputo - <vcaputo@pengaru.com>
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

/* The impetus for adding this is a desire for adding a variety of shapes
 * to modules/checkers.  I had started open-coding shapes like circle,
 * rhombus, pinwheel, and star, directly into checkers with a new style=
 * setting for choosing which to use instead of the plain filled square.
 *
 * But it seemed silly to bury this directly in checkers, when checkers
 * could trivially call into another module for rendering the filled
 * fragment.  And as the shapes became more interesting like star/pinwheel,
 * it also became clear that parameterizing them to really take advantage
 * of their procedural implementation would be a lot of fun.  Exposing
 * those parameters only as checkers settings, and knobs once available,
 * only within checkers, seemed like it'd be really selling things short.
 *
 * So here we are, shapes is its own module, it's kind of boring ATM.  Its
 * addition will likely be followed by checkers getting a filler module
 * setting, which could then invoke shapes - or any other module.
 *
 * TODO:
 *
 * - Add more interesting shapes
 *
 * - Parameterize more things, stuff like twist for the radial shapes
 *   comes to mind.  Twist at glance seems substantially complicated
 *   actually, since things are no longer just pinched/stretch circles with
 *   a single radial test to check.  It's like the non-convex polygon
 *   problem...
 *
 */


#include <assert.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_stream.h"
#include "til_tap.h"

#define SHAPES_DEFAULT_TYPE		SHAPES_TYPE_PINWHEEL
#define SHAPES_DEFAULT_SCALE		1
#define SHAPES_DEFAULT_POINTS		5
#define SHAPES_DEFAULT_SPIN		.1
#define SHAPES_DEFAULT_PINCH		.5
#define SHAPES_DEFAULT_PINCH_SPIN	.5
#define SHAPES_DEFAULT_PINCHES		0

#define SHAPES_SPIN_BASE		2.5f

typedef struct shapes_radcache_t shapes_radcache_t;

typedef enum shapes_type_t {
	SHAPES_TYPE_CIRCLE,
	SHAPES_TYPE_PINWHEEL,
	SHAPES_TYPE_RHOMBUS,
	SHAPES_TYPE_STAR,
} shapes_type_t;

typedef struct shapes_setup_t {
	til_setup_t		til_setup;
	shapes_type_t		type;
	float			scale;
	float			pinch;
	float			pinch_spin;
	unsigned		n_pinches;
	unsigned		n_points;
	float			spin;
} shapes_setup_t;

typedef struct shapes_context_t {
	til_module_context_t	til_module_context;
	shapes_setup_t		*setup;
	shapes_radcache_t	*radcache;

	struct {
		til_tap_t		scale;
		til_tap_t		pinch_factor;
		til_tap_t		pinch_spin_rate;
		til_tap_t		spin_rate;
		til_tap_t		n_pinches;
		til_tap_t		n_points;
	}			taps;

	struct {
		float			scale;
		float			pinch_factor;
		float			pinch_spin_rate;
		float			spin_rate;
		float			n_pinches;
		float			n_points;
	}			vars;

	float			*scale;
	float			*pinch_factor;
	float			*pinch_spin_rate;
	float			*spin_rate;
	float			*n_pinches;
	float			*n_points;

	float			spin, pinch_spin;
} shapes_context_t;

struct shapes_radcache_t {
	shapes_radcache_t	*next, *prev;
	unsigned		width, height;
	unsigned		refcount;
	unsigned		initialized:1;
	float			rads[];
};

static struct {
	shapes_radcache_t	*head;
	pthread_mutex_t		lock;
} shapes_radcache_list = { .lock = PTHREAD_MUTEX_INITIALIZER };


static void * shapes_radcache_unref(shapes_radcache_t *radcache)
{
	if (!radcache)
		return NULL;

	if (__sync_fetch_and_sub(&radcache->refcount, 1) == 1) {

		pthread_mutex_lock(&shapes_radcache_list.lock);
		if (radcache->prev)
			radcache->prev->next = radcache->next;
		else
			shapes_radcache_list.head = radcache->next;

		if (radcache->next)
			radcache->next->prev = radcache->prev;
		pthread_mutex_unlock(&shapes_radcache_list.lock);

		free(radcache);
	}

	return NULL;
}


static shapes_radcache_t * shapes_radcache_find(unsigned width, unsigned height)
{
	shapes_radcache_t	*radcache;

	pthread_mutex_lock(&shapes_radcache_list.lock);
	for (radcache = shapes_radcache_list.head; radcache; radcache = radcache->next) {
		if (radcache->width == width &&
		    radcache->height == height) {
			/* if we race with removal, refcount will be zero and we can't use it */
			if (!__sync_fetch_and_add(&radcache->refcount, 1))
				radcache = NULL;
			break;
		}
	}
	pthread_mutex_unlock(&shapes_radcache_list.lock);

	return radcache;
}


static shapes_radcache_t * shapes_radcache_new(unsigned width, unsigned height)
{
	size_t			size = width * height;
	shapes_radcache_t	*radcache;

	radcache = malloc(sizeof(shapes_radcache_t) + size * sizeof(radcache->rads[0]));
	assert(radcache);
	radcache->initialized = 0;
	radcache->width = width;
	radcache->height = height;
	radcache->refcount = 1;
	radcache->prev = NULL;

	pthread_mutex_lock(&shapes_radcache_list.lock);
	radcache->next = shapes_radcache_list.head;
	if (radcache->next)
		radcache->next->prev = radcache;
	pthread_mutex_unlock(&shapes_radcache_list.lock);

	return radcache;
}


static void shapes_update_taps(shapes_context_t *ctxt, til_stream_t *stream, float dt)
{
	/* FIXME: these vars probably need to be clamped within safe bounds to prevent crashing */
	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.scale))
		*ctxt->scale = ctxt->setup->scale;
	else
		ctxt->vars.scale = *ctxt->scale;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.pinch_factor))
		*ctxt->pinch_factor = ctxt->setup->pinch;
	else
		ctxt->vars.pinch_factor = *ctxt->pinch_factor;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.pinch_spin_rate))
		*ctxt->pinch_spin_rate = ctxt->setup->pinch_spin;
	else
		ctxt->vars.pinch_spin_rate = *ctxt->pinch_spin_rate;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.spin_rate))
		*ctxt->spin_rate = ctxt->setup->spin;
	else
		ctxt->vars.spin_rate = *ctxt->spin_rate;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.n_pinches))
		*ctxt->n_pinches = ctxt->setup->n_pinches;
	else
		ctxt->vars.n_pinches = *ctxt->n_pinches;

	if (ctxt->setup->type == SHAPES_TYPE_STAR ||
	    ctxt->setup->type == SHAPES_TYPE_PINWHEEL) {

		if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.n_points))
			*ctxt->n_points = ctxt->setup->n_points;
		else
			ctxt->vars.n_points = *ctxt->n_points;
	}

	ctxt->spin += dt * ctxt->vars.spin_rate * SHAPES_SPIN_BASE;
	ctxt->pinch_spin += dt * ctxt->vars.pinch_spin_rate * SHAPES_SPIN_BASE;
}


static til_module_context_t * shapes_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	shapes_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(shapes_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->setup = (shapes_setup_t *)setup;

	ctxt->taps.scale = til_tap_init_float(ctxt, &ctxt->scale, 1, &ctxt->vars.scale, "scale");
	ctxt->taps.pinch_factor = til_tap_init_float(ctxt, &ctxt->pinch_factor, 1, &ctxt->vars.pinch_factor, "pinch_factor");
	ctxt->taps.pinch_spin_rate = til_tap_init_float(ctxt, &ctxt->pinch_spin_rate, 1, &ctxt->vars.pinch_spin_rate, "pinch_spin_rate");
	ctxt->taps.spin_rate = til_tap_init_float(ctxt, &ctxt->spin_rate, 1, &ctxt->vars.spin_rate, "spin_rate");
	ctxt->taps.n_pinches = til_tap_init_float(ctxt, &ctxt->n_pinches, 1, &ctxt->vars.n_pinches, "n_pinches");
	if (ctxt->setup->type == SHAPES_TYPE_STAR ||
	    ctxt->setup->type == SHAPES_TYPE_PINWHEEL)
		ctxt->taps.n_points = til_tap_init_float(ctxt, &ctxt->n_points, 1, &ctxt->vars.n_points, "n_points");

	shapes_update_taps(ctxt, stream, 0.f);

	return &ctxt->til_module_context;
}


static void shapes_destroy_context(til_module_context_t *context)
{
	shapes_context_t	*ctxt = (shapes_context_t *)context;

	shapes_radcache_unref(ctxt->radcache);
	free(ctxt);
}


static void shapes_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	til_fb_fragment_t	*fragment = *fragment_ptr;
	shapes_context_t	*ctxt = (shapes_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu_x16 };

	/* TODO:
	 * I've implemented this ad-hoc here for shapes, but I think there's a case to be made that
	 * such caching should be generalized and added to til_stream_t in a generalized manner.
	 *
	 * So shapes should be able to just register a cache of arbitrary type and dimensions with
	 * some identifier which can then be discovered by shapes and others via that potentially
	 * well-known identifier.
	 *
	 * In a sense this is just a prototype of what part of that might look like... it's pretty clear
	 * that something like "atan2() of every pixel coordinate in a centered origin coordinate system"
	 * could have cached value to many modules
	 */
	{ /* radcache maintenance */
		shapes_radcache_t	*radcache = ctxt->radcache;

		if (radcache &&
		    (radcache->width != fragment->frame_width ||
		     radcache->height != fragment->frame_height))
			radcache = ctxt->radcache = shapes_radcache_unref(radcache);

		if (!radcache)
			radcache = shapes_radcache_find(fragment->frame_width, fragment->frame_height);

		if (!radcache)
			radcache = shapes_radcache_new(fragment->frame_width, fragment->frame_height);

		ctxt->radcache = radcache;
	}

	shapes_update_taps(ctxt, stream, (ticks - context->last_ticks) * .001f);
}


static void shapes_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	shapes_context_t	*ctxt = (shapes_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	unsigned		size = MIN(fragment->frame_width, fragment->frame_height) * ctxt->vars.scale;
	unsigned		xoff = (fragment->frame_width - size) >> 1;
	unsigned		yoff = (fragment->frame_height - size) >> 1;
	unsigned		yskip = (fragment->y > yoff ? (fragment->y - yoff) : 0);
	unsigned		xskip = (fragment->x > xoff ? (fragment->x - xoff) : 0);
	unsigned		ystart = MAX(fragment->y, yoff), yend = MIN(yoff + size, fragment->y + fragment->height);
	unsigned		xstart = MAX(fragment->x, xoff), xend = MIN(xoff + size, fragment->x + fragment->width);
	shapes_radcache_t	*radcache = ctxt->radcache;
	float			*rads = radcache->rads;

	if (!fragment->cleared) {
		/* when {letter,pillar}boxed we need to clear the padding */
		if (xoff > fragment->x) {
			for (int y = fragment->y; y < fragment->y + fragment->height; y++) {
				for (int x = fragment->x; x < xoff; x++)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);
				for (int x = fragment->frame_width - (size + xoff); x < fragment->x + fragment->width; x++)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);
			}
		}

		if (yoff > fragment->y) {
			for (int y = fragment->y; y < yoff; y++)
				for (int x = fragment->x; x < fragment->x + fragment->width; x++)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);

			for (int y = fragment->frame_height - (size + yoff); y < fragment->y + fragment->height; y++)
				for (int x = fragment->x; x < fragment->x + fragment->width; x++)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);
		}
	}

	/* eventually these should probably get broken out into functions,
	 * but it's not too unwieldy for now.
	 */
	switch (ctxt->setup->type) {
	case SHAPES_TYPE_CIRCLE: {
		int	r_sq = (size >> 1) * (size >> 1);
		float	s = 2.f / (float)size;
		float	XX, YY, YYY;
		int	X, Y;
		float	n_pinches, pinch, pinch_s;

		n_pinches = rintf(ctxt->vars.n_pinches);
		pinch_s = ctxt->vars.pinch_factor;
		pinch = ctxt->pinch_spin;

		YY = -1.f + yskip * s;
		Y = -(size >> 1) + yskip;
		for (unsigned y = ystart; y < yend; y++, Y++, YY += s) {
			XX = -1.f + xskip * s;
			X = -(size >> 1) + xskip;
			YYY = Y * Y;
			if (!radcache->initialized) {
				for (unsigned x = xstart; x < xend; x++, X++, XX += s) {
					float	a = rads[y * radcache->width + x] = atan2f(YY, XX);

					if (YYY+X*X < r_sq * (1.f - fabsf(sinf(n_pinches * a + pinch)) * pinch_s))
						til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff); /* TODO: stop relying on checked for clipping */
					else if (!fragment->cleared)
						til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);

				}
			} else {
				float	*rads = radcache->rads;
				for (unsigned x = xstart; x < xend; x++, X++, XX += s) {
					float	a = rads[y * radcache->width + x];

					if (YYY+X*X < r_sq * (1.f - fabsf(sinf(n_pinches * a + pinch)) * pinch_s))
						til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff); /* TODO: stop relying on checked for clipping */
					else if (!fragment->cleared)
						til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);

				}
			}
		}
		break;
	}

	case SHAPES_TYPE_PINWHEEL: {
		float	s = 2.f / (float)size;
		float	XX, YY, YYYY, pinch, spin, pinch_s;
		float	n_points, n_pinches;

		n_points = rintf(ctxt->vars.n_points);
		n_pinches = rintf(ctxt->vars.n_pinches);
		pinch_s = ctxt->vars.pinch_factor;
		spin = ctxt->spin;
		pinch = ctxt->pinch_spin;

		YY = -1.f + yskip * s;
		for (unsigned y = ystart; y < yend; y++, YY += s) {
			XX = -1.f + xskip * s;
			YYYY = YY * YY;
			if (!radcache->initialized) {
				for (unsigned x = xstart; x < xend; x++, XX += s) {
					float	a = rads[y * radcache->width + x] = atan2f(YY, XX);
					float	r = cosf(n_points * (a + spin)) * .5f + .5f;

					r *= 1.f - fabsf(sinf(n_pinches * (a + pinch))) * pinch_s;

					if (XX * XX + YYYY < r * r)
						til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff);
					else if (!fragment->cleared)
						til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);

				}
			} else {
				for (unsigned x = xstart; x < xend; x++, XX += s) {
					float	a = rads[y * radcache->width + x];
					float	r = cosf(n_points * (a + spin)) * .5f + .5f;

					r *= 1.f - fabsf(sinf(n_pinches * (a + pinch))) * pinch_s;

					if (XX * XX + YYYY < r * r)
						til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff);
					else if (!fragment->cleared)
						til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);

				}
			}
		}
		break;
	}

	case SHAPES_TYPE_RHOMBUS: {
		float	s = 2.f / (float)size;
		int	r = (size >> 1);
		float	XX, YY;
		int	X, Y;
		float	n_pinches, pinch, pinch_s;

		n_pinches = rintf(ctxt->vars.n_pinches);
		pinch_s = ctxt->vars.pinch_factor;
		pinch = ctxt->pinch_spin;

		YY = -1.f + yskip * s;
		Y = -(size >> 1) + yskip;
		for (unsigned y = ystart; y < yend; y++, Y++, YY += s) {
			float	*rads = radcache->rads;
			XX = -1.f + xskip * s;
			X = -(size >> 1) + xskip;
			if (!radcache->initialized) {
				for (unsigned x = xstart; x < xend; x++, X++, XX += s) {
					float	a = rads[y * radcache->width + x] = atan2f(YY, XX);

					if (abs(Y) + abs(X) < r * (1.f - fabsf(sinf(n_pinches * a + pinch)) * pinch_s))
						til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff);
					else if (!fragment->cleared)
						til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);

				}
			} else {
				for (unsigned x = xstart; x < xend; x++, X++, XX += s) {
					float	a = rads[y * radcache->width + x];

					if (abs(Y) + abs(X) < r * (1.f - fabsf(sinf(n_pinches * a + pinch)) * pinch_s))
						til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff);
					else if (!fragment->cleared)
						til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);

				}
			}
		}
		break;
	}

	case SHAPES_TYPE_STAR: {
		float	s = 2.f / (float)size;
		float	XX, YY, YYYY, pinch, spin, pinch_s;
		float	n_points, n_pinches;

		n_points = rintf(ctxt->vars.n_points);
		n_pinches = rintf(ctxt->vars.n_pinches);
		pinch_s = ctxt->vars.pinch_factor;
		spin = ctxt->spin;
		pinch = ctxt->pinch_spin;

		YY = -1.f + yskip * s;
		for (unsigned y = ystart; y < yend; y++, YY += s) {
			XX = -1.f + xskip * s;
			YYYY = YY * YY;
			if (!radcache->initialized) {
				for (unsigned x = xstart; x < xend; x++, XX += s) {
					float	a = rads[y * radcache->width + x] = atan2f(YY, XX);
					float	r = (M_2_PI * asinf(sinf(n_points * (a + spin)) * .5f + .5f)) * .5f + .5f;
						/*   ^^^^^^^^^^^^^^^^^^^ approximates a triangle wave */

					r *= 1.f - fabsf(sinf(n_pinches * a + pinch)) * pinch_s;

					if (XX * XX + YYYY < r * r)
						til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff);
					else if (!fragment->cleared)
						til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);
				}
			} else {
				float	*rads = radcache->rads;
				for (unsigned x = xstart; x < xend; x++, XX += s) {
					float	a = rads[y * radcache->width + x];
					float	r = (M_2_PI * asinf(sinf(n_points * (a + spin)) * .5f + .5f)) * .5f + .5f;
						/*   ^^^^^^^^^^^^^^^^^^^ approximates a triangle wave */

					r *= 1.f - fabsf(sinf(n_pinches * a + pinch)) * pinch_s;

					if (XX * XX + YYYY < r * r)
						til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff);
					else if (!fragment->cleared)
						til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x0);
				}
			}
		}
		break;
	}
	}
}


static void shapes_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned int ticks, til_fb_fragment_t **fragment_ptr)
{
	shapes_context_t	*ctxt = (shapes_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	/* XXX: note that in rendering, initialized is checked racily and it's entirely possible
	 * for multiple contexts to be rendering and populating the radcache when !initialized
	 * simultaneously... but since they'd be producing identical data for the cache anyways,
	 * it seems mostly harmless for now.  What should probably be done is make initialized a
	 * tri-state that's atomically advanced towards initialized wiht an "intializing" mid-state
	 * that only one renderer can enter, then the others treat "initializing" as !radcache at all
	 * TODO FIXME
	 *
	 * XXX: also note the radcache must be prevented from getting considered initialized by a partial
	 * frame - which happens as checkers::fill_module when the edge cells overhang for centering.
	 * Those perimeter renders won't populate the radcache fully.  This is a band-aid, it would be
	 * better to let the radcache's initialized area expand so it can accelerate those perimiter
	 * cases with the partially initialized contents if there's enough there, then once full-frame cells
	 * happen it just grows with the first one of those. For now this check fixes a bug. FIXME TODO
	 */
	if (fragment->width == fragment->frame_width &&
	    fragment->height == fragment->frame_height)
		ctxt->radcache->initialized = 1;
}


static int shapes_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	shapes_module = {
	.create_context = shapes_create_context,
	.destroy_context = shapes_destroy_context,
	.prepare_frame = shapes_prepare_frame,
	.render_fragment = shapes_render_fragment,
	.finish_frame = shapes_finish_frame,
	.setup = shapes_setup,
	.name = "shapes",
	.description = "Procedural 2D shapes (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


static int shapes_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*type;
	const char	*points;
	const char	*spin;
	const char	*scale;
	const char	*pinch;
	const char	*pinch_spin;
	const char	*pinches;
	const char	*type_values[] = {
				"circle",
				"pinwheel",
				"rhombus",
				"star",
				NULL
			};
	const char	*points_values[] = {
				"3",
				"4",
				"5",
				"6",
				"7",
				"8",
				"9",
				"10",
				"11",
				"12",
				"13",
				"14",
				"15",
				"16",
				"17",
				"18",
				"19",
				"20",
				NULL
			};
	const char	*spin_values[] = {
				"-1",
				"-.9",
				"-.75",
				"-.5",
				"-.25",
				"-.1",
				"-.01",
				"0",
				".01",
				".1",
				".25",
				".5",
				".75",
				".9",
				"1",
				NULL
			};
	const char	*scale_values[] = {
				/* It's unclear to me if this even makes sense, but I can see some
				 * value in permitting something of a margin to exist around the shape.
				 * For that reason I'm not going smaller than 50%.
				 */
				".5",
				".66",
				".75",
				".9",
				"1",
				NULL
			};
	const char	*pinch_values[] = {
				"0",
				".1",
				".25",
				".33",
				".5",
				".66",
				".75",
				".9",
				"1",
				NULL
			};
	const char	*pinches_values[] = {
				"0",
				"1",
				"2",
				"3",
				"4",
				"5",
				"6",
				"7",
				"8",
				"9",
				"10",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Shape type",
							.key = "type",
							.regex = "[a-zA-Z]+",
							.preferred = type_values[SHAPES_DEFAULT_TYPE],
							.values = type_values,
							.annotations = NULL
						},
						&type,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (!strcasecmp(type, "star") || !strcasecmp(type, "pinwheel")) {
		r = til_settings_get_and_describe_value(settings,
							&(til_setting_spec_t){
								.name = "Number of points",
								.key = "points",
								.regex = "[0-9]+",
								.preferred = TIL_SETTINGS_STR(SHAPES_DEFAULT_POINTS),
								.values = points_values,
								.annotations = NULL
							},
							&points,
							res_setting,
							res_desc);
		if (r)
			return r;

		r = til_settings_get_and_describe_value(settings,
							&(til_setting_spec_t){
								.name = "Spin factor",
								.key = "spin",
								.regex = "-?(0|1|0?\\.[0-9]{1,2})", /* Derived from pixbounce, I'm sure when regexes start getting actually applied we're going to have to revisit all of these and fix them with plenty of lols. */
								.preferred = TIL_SETTINGS_STR(SHAPES_DEFAULT_SPIN),
								.values = spin_values,
								.annotations = NULL
							},
							&spin,
							res_setting,
							res_desc);
		if (r)
			return r;
	}

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Scaling factor",
							.key = "scale",
							.regex = "(1|0?\\.[0-9]{1,2})",
							.preferred = TIL_SETTINGS_STR(SHAPES_DEFAULT_SCALE),
							.values = scale_values,
							.annotations = NULL
						},
						&scale,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Number of pinches",
							.key = "pinches",
							.regex = "[0-9]+",
							.preferred = TIL_SETTINGS_STR(SHAPES_DEFAULT_PINCHES),
							.values = pinches_values,
							.annotations = NULL
						},
						&pinches,
						res_setting,
						res_desc);
	if (r)
		return r;

	/* since n_pinches is tapped, it can abruptly become non-zero, so let's always initialize the pinches-dependent settings */
	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Pinch spin factor",
							.key = "pinch_spin",
							.regex = "-?(0|1|0?\\.[0-9]{1,2})",
							.preferred = TIL_SETTINGS_STR(SHAPES_DEFAULT_PINCH_SPIN),
							.values = spin_values,
							.annotations = NULL
						},
						&pinch_spin,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Pinch factor",
							.key = "pinch",
							.regex = "(1|0?\\.[0-9]{1,2})",
							.preferred = TIL_SETTINGS_STR(SHAPES_DEFAULT_PINCH),
							.values = pinch_values,
							.annotations = NULL
						},
						&pinch,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		int	i;

		shapes_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &shapes_module);
		if (!setup)
			return -ENOMEM;

		for (i = 0; type_values[i]; i++) {
			if (!strcasecmp(type, type_values[i])) {
				setup->type = i;
				break;
			}
		}

		if (!type_values[i]) {
			til_setup_free(&setup->til_setup);
			return -EINVAL;
		}

		sscanf(scale, "%f", &setup->scale); /* TODO: -EINVAL parse errors */
		sscanf(pinches, "%u", &setup->n_pinches); /* TODO: -EINVAL parse errors */
		sscanf(pinch_spin, "%f", &setup->pinch_spin); /* TODO: -EINVAL parse errors */
		sscanf(pinch, "%f", &setup->pinch); /* TODO: -EINVAL parse errors */

		if (setup->type == SHAPES_TYPE_STAR || setup->type == SHAPES_TYPE_PINWHEEL) {
			sscanf(points, "%u", &setup->n_points); /* TODO: -EINVAL parse errors */
			sscanf(spin, "%f", &setup->spin); /* TODO: -EINVAL parse errors */
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}
