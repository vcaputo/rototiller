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
 * - Go threaded, for ease of implementation this is currently simple
 *   non-threaded code.  In the checkers use case, the individual checkers
 *   are already being rendered concurrently, so as-is this still becomes
 *   threaded there.  It's just full-frame shapes situations where it
 *   hurts.
 *
 */


#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

#define SHAPES_DEFAULT_TYPE		SHAPES_TYPE_PINWHEEL
#define SHAPES_DEFAULT_SCALE		1
#define SHAPES_DEFAULT_POINTS		5
#define SHAPES_DEFAULT_SPIN		.1
#define SHAPES_DEFAULT_PINCH		0
#define SHAPES_DEFAULT_PINCH_SPIN	.5
#define SHAPES_DEFAULT_PINCHES		2

#define SHAPES_SPIN_BASE		.0025f

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
	shapes_setup_t		setup;
} shapes_context_t;


static shapes_setup_t shapes_default_setup = {
	.type = SHAPES_DEFAULT_TYPE,
};


static til_module_context_t * shapes_create_context(unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	shapes_context_t	*ctxt;

	if (!setup)
		setup = &shapes_default_setup.til_setup;

	ctxt = til_module_context_new(sizeof(shapes_context_t), seed, ticks, n_cpus, path);
	if (!ctxt)
		return NULL;

	ctxt->setup = *(shapes_setup_t *)setup;

	return &ctxt->til_module_context;
}


static void shapes_render_fragment(til_module_context_t *context, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	shapes_context_t	*ctxt = (shapes_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	unsigned		size = MIN(fragment->frame_width, fragment->frame_height) * ctxt->setup.scale;
	unsigned		xoff = (fragment->frame_width - size) >> 1;
	unsigned		yoff = (fragment->frame_height - size) >> 1;

	if (!fragment->cleared) {
		/* when {letter,pillar}boxed we need to clear the padding */
		if (xoff > fragment->x) {
			for (int y = fragment->y; y < fragment->y + fragment->height; y++) {
				for (int x = fragment->x; x < xoff; x++)
					til_fb_fragment_put_pixel_checked(fragment, 0, fragment->x, fragment->y, 0x0);
				for (int x = fragment->frame_width - (size + xoff); x < fragment->x + fragment->width; x++)
					til_fb_fragment_put_pixel_checked(fragment, 0, fragment->x, fragment->y, 0x0);
			}
		}

		if (yoff > fragment->y) {
			for (int y = fragment->y; y < yoff; y++)
				for (int x = fragment->x; x < fragment->x + fragment->width; x++)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x, fragment->y, 0x0);

			for (int y = fragment->frame_height - (size + yoff); y < fragment->y + fragment->height; y++)
				for (int x = fragment->x; x < fragment->x + fragment->width; x++)
					til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x, fragment->y, 0x0);
		}
	}

	/* eventually these should probably get broken out into functions,
	 * but it's not too unwieldy for now.
	 */
	switch (ctxt->setup.type) {
	case SHAPES_TYPE_CIRCLE: {
		unsigned	yskip = (fragment->y > yoff ? (fragment->y - yoff) : 0);
		unsigned	xskip = (fragment->x > xoff ? (fragment->x - xoff) : 0);
		int		r_sq = (size >> 1) * (size >> 1);
		float		s = 2.f / (float)size;
		float		XX, YY;
		int		X, Y;

		YY = -1.f + yskip * s;
		Y = -(size >> 1) + yskip;
		for (unsigned y = MAX(fragment->y, yoff); y < yoff + size; y++, Y++, YY += s) {
			XX = -1.f + xskip * s;
			X = -(size >> 1) + xskip;
			for (unsigned x = MAX(fragment->x, xoff); x < xoff + size; x++, X++, XX += s) {
				float	rad = atan2f(YY, XX);

				if (Y*Y+X*X < r_sq * (1.f - fabsf(cosf(ctxt->setup.n_pinches * rad + (float)ticks * ctxt->setup.pinch_spin * SHAPES_SPIN_BASE)) * ctxt->setup.pinch))
					til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff); /* TODO: stop relying on checked for clipping */
				else if (!fragment->cleared)
					til_fb_fragment_put_pixel_checked(fragment, 0, x, y, 0x0);

			}
		}
		break;
	}

	case SHAPES_TYPE_PINWHEEL: {
		unsigned	yskip = (fragment->y > yoff ? (fragment->y - yoff) : 0);
		unsigned	xskip = (fragment->x > xoff ? (fragment->x - xoff) : 0);
		float		s = 2.f / (float)size;
		float		XX, YY;

		YY = -1.f + yskip * s;
		for (unsigned y = MAX(fragment->y, yoff); y < yoff + size; y++, YY += s) {
			XX = -1.f + xskip * s;
			for (unsigned x = MAX(fragment->x, xoff); x < xoff + size; x++, XX += s) {
				float	rad = atan2f(YY, XX);
				float	r = cosf((float)ctxt->setup.n_points * (rad + (float)ticks * ctxt->setup.spin * SHAPES_SPIN_BASE)) * .5f + .5f;

				r *= 1.f - fabsf(cosf(ctxt->setup.n_pinches * rad + (float)ticks * ctxt->setup.pinch_spin * SHAPES_SPIN_BASE)) * ctxt->setup.pinch;

				if (XX * XX + YY * YY < r * r)
					til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff); /* stop relying on checked for clipping */
				else if (!fragment->cleared)
					til_fb_fragment_put_pixel_checked(fragment, 0, x, y, 0x0);

			}
		}
		break;
	}

	case SHAPES_TYPE_RHOMBUS: {
		unsigned	yskip = (fragment->y > yoff ? (fragment->y - yoff) : 0);
		unsigned	xskip = (fragment->x > xoff ? (fragment->x - xoff) : 0);
		float		s = 2.f / (float)size;
		int		r = (size >> 1);
		float		XX, YY;
		int		X, Y;

		YY = -1.f + yskip * s;
		Y = -(size >> 1) + yskip;
		for (unsigned y = MAX(fragment->y, yoff); y < yoff + size; y++, Y++, YY += s) {
			XX = -1.f + xskip * s;
			X = -(size >> 1) + xskip;
			for (unsigned x = MAX(fragment->x, xoff); x < xoff + size; x++, X++, XX += s) {
				float	rad = atan2f(YY, XX);

				if (abs(Y) + abs(X) < r * (1.f - fabsf(cosf(ctxt->setup.n_pinches * rad + (float)ticks * ctxt->setup.pinch_spin * SHAPES_SPIN_BASE)) * ctxt->setup.pinch))
					til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff);
				else if (!fragment->cleared)
					til_fb_fragment_put_pixel_checked(fragment, 0, x, y, 0x0);

			}
		}
		break;
	}

	case SHAPES_TYPE_STAR: {
		unsigned	yskip = (fragment->y > yoff ? (fragment->y - yoff) : 0);
		unsigned	xskip = (fragment->x > xoff ? (fragment->x - xoff) : 0);
		float		s = 2.f / (float)size;
		float		XX, YY;

		YY = -1.f + yskip * s;
		for (unsigned y = MAX(fragment->y, yoff); y < yoff + size; y++, YY += s) {
			XX = -1.f + xskip * s;
			for (unsigned x = MAX(fragment->x, xoff); x < xoff + size; x++, XX += s) {
				float	rad = atan2f(YY, XX);
				float	r = (M_2_PI * asinf(sinf((float)ctxt->setup.n_points * (rad + (float)ticks * ctxt->setup.spin * SHAPES_SPIN_BASE)) * .5f + .5f)) * .5f + .5f;
					/*   ^^^^^^^^^^^^^^^^^^^ approximates a triangle wave */

				r *= 1.f - fabsf(cosf(ctxt->setup.n_pinches * rad + (float)ticks * ctxt->setup.pinch_spin * SHAPES_SPIN_BASE)) * ctxt->setup.pinch;

				if (XX * XX + YY * YY < r * r)
					til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff);
				else if (!fragment->cleared)
					til_fb_fragment_put_pixel_checked(fragment, 0, x, y, 0x0);
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

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
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

	if (strcasecmp(pinch, "0")) {
		r = til_settings_get_and_describe_value(settings,
							&(til_setting_desc_t){
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
							&(til_setting_desc_t){
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
	}

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
		sscanf(pinch, "%f", &setup->pinch); /* TODO: -EINVAL parse errors */
		if (setup->pinch != 0) {
			sscanf(pinch_spin, "%f", &setup->pinch_spin); /* TODO: -EINVAL parse errors */
			sscanf(pinches, "%u", &setup->n_pinches); /* TODO: -EINVAL parse errors */
		}

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
	.render_fragment = shapes_render_fragment,
	.setup = shapes_setup,
	.name = "shapes",
	.description = "Procedural 2D shapes",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
