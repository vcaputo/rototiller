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
 * - Expose parameters as settings
 *
 * - Parameterize more things, stuff like twist for the radial shapes
 *   comes to mind.  The simplistic CCW rotation of star/pinwheel should be
 *   more variable and exposed as settings, etc.
 *
 * - Go threaded, for ease of implementation this is currently simple
 *   non-threaded code.  In the checkers use case, the individual checkers
 *   are already being rendered concurrently, so as-is this still becomes
 *   threaded there.  It's just full-frame shapes situations where it
 *   hurts.
 *
 * - The presently static shapes like circle and rhombus could be simply
 *   rendered once @ context_create() into a dense buffer then copied at
 *   render_fragment() time.  The current implementation is very naive and
 *   slow procedurally redrawing even these constant shapes.  But the
 *   assumption is as more parameterizing is added, all the shapes will
 *   become dynamic.  So there's no sense adding a cache.
 */


#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"

#define SHAPES_DEFAULT_TYPE	SHAPES_TYPE_PINWHEEL
#define SHAPES_DEFAULT_SCALE	1
#define SHAPES_DEFAULT_POINTS	5
#define SHAPES_DEFAULT_SPIN	.1

typedef enum shapes_type_t {
	SHAPES_TYPE_CIRCLE,
	SHAPES_TYPE_PINWHEEL,
	SHAPES_TYPE_RHOMBUS,
	SHAPES_TYPE_STAR,
} shapes_type_t;

typedef struct shapes_setup_t {
	til_setup_t	til_setup;
	shapes_type_t	type;
	float		scale;
	unsigned	n_points;
	float		spin;
} shapes_setup_t;

typedef struct shapes_context_t {
	shapes_setup_t	setup;
} shapes_context_t;


static shapes_setup_t shapes_default_setup = {
	.type = SHAPES_DEFAULT_TYPE,
};


static void * shapes_create_context(unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	shapes_context_t	*ctxt;

	if (!setup)
		setup = &shapes_default_setup.til_setup;

	ctxt = calloc(1, sizeof(shapes_context_t));
	if (!ctxt)
		return NULL;

	ctxt->setup = *(shapes_setup_t *)setup;

	return ctxt;
}


static void shapes_destroy_context(void *context)
{
	shapes_context_t	*ctxt = context;

	free(ctxt);
}


static void shapes_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	shapes_context_t	*ctxt = context;
	unsigned		size = MIN(fragment->width, fragment->height) * ctxt->setup.scale;
	unsigned		xoff = (fragment->width - size) >> 1;
	unsigned		yoff = (fragment->height - size) >> 1;

	if (!fragment->cleared) {
		/* when {letter,pillar}boxed we need to clear the padding */
		if (xoff) {
			for (int y = 0; y < fragment->height; y++) {
				for (int x = 0; x < xoff; x++)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, 0x0);
				for (int x = fragment->width - (size + xoff); x < fragment->width; x++)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, 0x0);
			}
		}

		if (yoff) {
			for (int y = 0; y < yoff; y++)
				for (int x = 0; x < fragment->width; x++)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, 0x0);

			for (int y = fragment->height - (size + yoff); y < fragment->height; y++)
				for (int x = 0; x < fragment->width; x++)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, 0x0);
		}
	}

	/* eventually these should probably get broken out into functions,
	 * but it's not too unwieldy for now.
	 */
	switch (ctxt->setup.type) {
	case SHAPES_TYPE_CIRCLE: {
		int	r_sq = (size >> 1);

		r_sq *= r_sq;

		for (int y = yoff, Y = -(size >> 1); y < yoff + size; y++, Y++) {
			for (int x = xoff, X = -(size >> 1); x < xoff + size; x++, X++) {
				if (Y*Y+X*X < r_sq)
					til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, fragment->x + x, fragment->y + y, 0xffffffff);
				else if (!fragment->cleared)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, 0x0);

			}
		}
		break;
	}

	case SHAPES_TYPE_PINWHEEL: {
			float	s = 2.f / (float)size;
			float	X, Y;

			Y = -1.f;
			for (unsigned y = yoff; y < yoff + size; y++, Y += s) {
				X = -1.f;
				for (unsigned x = xoff; x < xoff + size; x++, X += s) {
					float	rad = atan2f(Y, X) + (float)ticks * ctxt->setup.spin * .01f;
					float	r = cosf((float)ctxt->setup.n_points * rad) * .5f + .5f;

					if (X * X + Y * Y < r * r)
						til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, fragment->x + x, fragment->y + y, 0xffffffff);
					else if (!fragment->cleared)
						til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, 0x0);

				}
			}
		break;
	}

	case SHAPES_TYPE_RHOMBUS: {
		int	r = (size >> 1);
		int	X, Y;

		Y = -(size >> 1);
		for (unsigned y = yoff; y < yoff + size; y++, Y++) {
			X = -(size >> 1);
			for (unsigned x = xoff; x < xoff + size; x++, X++) {
				if (abs(Y) + abs(X) < r)
					til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, fragment->x + x, fragment->y + y, 0xffffffff);
				else if (!fragment->cleared)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, 0x0);

			}
		}
		break;
	}

	case SHAPES_TYPE_STAR: {
			float	s = 2.f / (float)size;
			float	X, Y;

			Y = -1.f;
			for (unsigned y = yoff; y < yoff + size; y++, Y += s) {
				X = -1.f;
				for (unsigned x = xoff; x < xoff + size; x++, X += s) {
					float	rad = atan2f(Y, X) + (float)ticks * ctxt->setup.spin * .01f;
					float	r = (M_2_PI * asinf(sinf((float)ctxt->setup.n_points * rad) * .5f + .5f)) * .5f + .5f;
						/*   ^^^^^^^^^^^^^^^^^^^ approximates a triangle wave */

					if (X * X + Y * Y < r * r)
						til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, fragment->x + x, fragment->y + y, 0xffffffff);
					else if (!fragment->cleared)
						til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, 0x0);
				}
			}
		break;
	}
	}
}


static int shapes_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*type;
	const char	*points;
	const char	*spin;
	const char	*scale;
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
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
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

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
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

	if (!strcasecmp(type, "star") || !strcasecmp(type, "pinwheel")) {
		r = til_settings_get_and_describe_value(settings,
							&(til_setting_desc_t){
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
							&(til_setting_desc_t){
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

	if (res_setup) {
		int	i;

		shapes_setup_t	*setup;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
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

		if (setup->type == SHAPES_TYPE_STAR || setup->type == SHAPES_TYPE_PINWHEEL) {
			sscanf(points, "%u", &setup->n_points); /* TODO: -EINVAL parse errors */
			sscanf(spin, "%f", &setup->spin); /* TODO: -EINVAL parse errors */
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	shapes_module = {
	.create_context = shapes_create_context,
	.destroy_context = shapes_destroy_context,
	.render_fragment = shapes_render_fragment,
	.setup = shapes_setup,
	.name = "shapes",
	.description = "Procedural 2D shapes",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
