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

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"


#define CHECKERS_DEFAULT_SIZE		32
#define CHECKERS_DEFAULT_PATTERN	CHECKERS_PATTERN_CHECKERED
#define CHECKERS_DEFAULT_DYNAMICS	CHECKERS_DYNAMICS_ODD
#define CHECKERS_DEFAULT_DYNAMICS_RATE	1.0
#define CHECKERS_DEFAULT_FILL		CHECKERS_FILL_COLOR
#define CHECKERS_DEFAULT_COLOR		0xffffff
#define CHECKERS_DEFAULT_FILL_MODULE	"none"


typedef enum checkers_pattern_t {
	CHECKERS_PATTERN_CHECKERED,
	CHECKERS_PATTERN_RANDOM,
} checkers_pattern_t;

typedef enum checkers_dynamics_t {
	CHECKERS_DYNAMICS_ODD,
	CHECKERS_DYNAMICS_EVEN,
	CHECKERS_DYNAMICS_ALTERNATING,
	CHECKERS_DYNAMICS_RANDOM,
} checkers_dynamics_t;

typedef enum checkers_fill_t {
	CHECKERS_FILL_COLOR,
	CHECKERS_FILL_SAMPLED,
	CHECKERS_FILL_TEXTURED,
	CHECKERS_FILL_RANDOM,
	CHECKERS_FILL_MIXED,
} checkers_fill_t;

typedef struct checkers_setup_t {
	til_setup_t		til_setup;
	unsigned		size;
	checkers_pattern_t	pattern;
	checkers_dynamics_t	dynamics;
	float			rate;
	checkers_fill_t		fill;
	uint32_t		color;
	const til_module_t	*fill_module;
} checkers_setup_t;

typedef struct checkers_context_t {
	til_module_context_t	til_module_context;
	checkers_setup_t	setup;
	til_module_context_t	*fill_module_contexts[];
} checkers_context_t;


static checkers_setup_t checkers_default_setup = {
	.size = CHECKERS_DEFAULT_SIZE,
	.pattern = CHECKERS_DEFAULT_PATTERN,
	.dynamics = CHECKERS_DEFAULT_DYNAMICS,
	.rate = CHECKERS_DEFAULT_DYNAMICS_RATE,
	.color = CHECKERS_DEFAULT_COLOR,
};


static til_module_context_t * checkers_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	size_t			size = sizeof(checkers_context_t);
	checkers_context_t	*ctxt;

	if (!setup)
		setup = &checkers_default_setup.til_setup;

	if (((checkers_setup_t *)setup)->fill_module)
		size += sizeof(til_module_context_t *) * n_cpus;

	ctxt = til_module_context_new(module, size, stream, ticks, seed, n_cpus, path);
	if (!ctxt)
		return NULL;

	ctxt->setup = *(checkers_setup_t *)setup;

	if (ctxt->setup.fill_module) {
		const til_module_t	*module = ctxt->setup.fill_module;
		til_setup_t		*module_setup = NULL;

		(void) til_module_randomize_setup(module, seed, &module_setup, NULL);

		/* since checkers is already threaded, create an n_cpus=1 context per-cpu */
		for (unsigned i = 0; i < n_cpus; i++) /* TODO: errors */
			(void) til_module_create_context(module, stream, seed, ticks, 1, path /* FIXME TODO path-per-context breaks down on these per-cpu-context abuses */, module_setup, &ctxt->fill_module_contexts[i]);

		/* XXX: it would be interesting to support various patterns/layouts by varying the seed, but this will require
		 * more complex context allocation strategies while also maintaining the per-cpu allocation.
		 */

		til_setup_free(module_setup);
	}

	return &ctxt->til_module_context;
}


static void checkers_destroy_context(til_module_context_t *context)
{
	checkers_context_t	*ctxt = (checkers_context_t *)context;

	if (ctxt->setup.fill_module) {
		for (unsigned i = 0; i < context->n_cpus; i++)
			til_module_context_free(ctxt->fill_module_contexts[i]);
	}

	free(ctxt);
}


/* This is derived from til_fb_fragment_tile_single() with two variations:
 * 1. when the size doesn't align with frame size, the start tiles are offset
 *    to center the checkers letting the edge checkers all clip as needed
 * 2. the incoming frame width isn't propagated down to the tiled fragments,
 *    though for the potentially clipped boundary tiles the frame_{width,height}
 *    won't match the incoming width,height.
 *
 * XXX note this fragmenter in particular really exercises fill_modules' correct handling
 *     of frame vs. fragment dimensions and clipping semantics
 */
int checkers_fragment_tile_single(const til_fb_fragment_t *fragment, unsigned tile_size, unsigned number, til_fb_fragment_t *res_fragment)
{
	unsigned	w = fragment->width / tile_size, h = fragment->height / tile_size;
	unsigned	tiled_w = w * tile_size, tiled_h = h * tile_size;
	unsigned	x, y, xoff, yoff, xshift = 0, yshift = 0;

	assert(fragment);
	assert(res_fragment);

	/* Detect the need for fractional tiles on both axis and shift the fragments
	 * to keep the overall checkered output centered.  This complicates res_fragment.{x,y,width,height}
	 * calculations for the peripheral checker tiles as those must clip when shifted.
	 */
	if (tiled_w < fragment->width) {
		tiled_w += tile_size;
		xshift = (tiled_w - fragment->width) >> 1;
		w++;
	}

	if (tiled_h < fragment->height) {
		tiled_h += tile_size;
		yshift = (tiled_h - fragment->height) >> 1;
		h++;
	}

	y = number / w;
	if (y >= h)
		return 0;

	x = number - (y * w);

	xoff = x * tile_size;
	yoff = y * tile_size;

	if (fragment->texture) {
		assert(res_fragment->texture);
		assert(fragment->frame_width == fragment->texture->frame_width);
		assert(fragment->frame_height == fragment->texture->frame_height);
		assert(fragment->width == fragment->texture->width);
		assert(fragment->height == fragment->texture->height);
		assert(fragment->x == fragment->texture->x);
		assert(fragment->y == fragment->texture->y);

		*(res_fragment->texture) = (til_fb_fragment_t){
					.buf = fragment->texture->buf + (yoff * fragment->texture->pitch) - (y ? (yshift * fragment->texture->pitch) : 0) + (xoff - (x ? xshift : 0)),
					.width = MIN(fragment->width - (xoff - xshift), x ? tile_size : (tile_size - xshift)),
					.height = MIN(fragment->height - (yoff - yshift), y ? tile_size : (tile_size - yshift)),
					.x = x ? 0 : xshift,
					.y = y ? 0 : yshift,
					.frame_width = tile_size,
					.frame_height = tile_size,
					.stride = fragment->texture->stride + (fragment->width - MIN(fragment->width - (xoff - xshift), x ? tile_size : (tile_size - xshift))),
					.pitch = fragment->texture->pitch,
					.cleared = fragment->texture->cleared,
				};
	}

	*res_fragment = (til_fb_fragment_t){
				.texture = fragment->texture ? res_fragment->texture : NULL,
				/* TODO: copy pasta! */
				.buf = fragment->buf + (yoff * fragment->pitch) - (y ? (yshift * fragment->pitch) : 0) + (xoff - (x ? xshift : 0)),
				.width = MIN(fragment->width - (xoff - xshift), x ? tile_size : (tile_size - xshift)),
				.height = MIN(fragment->height - (yoff - yshift), y ? tile_size : (tile_size - yshift)),
				.x = x ? 0 : xshift,
				.y = y ? 0 : yshift,
				// this is a little janky but leave frame_width to be set by render_fragment
				// so it can use the old frame_width for determining checkered state
				.frame_width = fragment->width, // becomes tile_size
				.frame_height = fragment->height, // becomes tile_size
				.stride = fragment->stride + (fragment->width - MIN(fragment->width - (xoff - xshift), x ? tile_size : (tile_size - xshift))),
				.pitch = fragment->pitch,
				.number = number,
				.cleared = fragment->cleared,
			};

	return 1;
}


static int checkers_fragmenter(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	checkers_context_t	*ctxt = (checkers_context_t *)context;

	return checkers_fragment_tile_single(fragment, ctxt->setup.size, number, res_fragment);
}


static void checkers_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	checkers_context_t	*ctxt = (checkers_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	/* XXX: note cpu_affinity is required when fill_module is used, to ensure module_contexts
	 * have a stable relationship to fragnum.  Otherwise the output would be unstable because the
	 * module contexts would be randomly distributed across the filled checkers frame-to-frame.
	 * This is unfortunate since cpu_affinity is likely to be slower than just letting the render
	 * threads render fragments in whatever order (the preferred default).  fill_module here
	 * is actually *the* reason til_frame_plan_t.cpu_affinity got implemented, before this there
	 * wasn't even a til_frame_plan_t container; a bare til_fragmenter_t was returned.
	 */
	*res_frame_plan = (til_frame_plan_t){ .fragmenter = checkers_fragmenter, .cpu_affinity = !!ctxt->setup.fill_module };
}


static inline unsigned hash(unsigned x)
{
	x = ((x >> 16) ^ x) * 0x61C88647;
	x = ((x >> 16) ^ x) * 0x61C88647;
	x = ((x >> 16) ^ x) * 0x61C88647;
	x = (x >> 16) ^ x;

	return x;
}


static void checkers_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	checkers_context_t	*ctxt = (checkers_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	uint32_t		color = ctxt->setup.color, flags = 0;
	checkers_fill_t		fill = ctxt->setup.fill;
	int			state;

	switch (ctxt->setup.pattern) {
	case CHECKERS_PATTERN_CHECKERED: {
		unsigned	tiles_per_row, row, col;

		tiles_per_row = fragment->frame_width / ctxt->setup.size;
		if (tiles_per_row * ctxt->setup.size < fragment->frame_width)
			tiles_per_row++;

		row = fragment->number / tiles_per_row;
		col = fragment->number % tiles_per_row;
		state = (row ^ col) & 0x1;
		break;
	}
	case CHECKERS_PATTERN_RANDOM:
		state = hash(fragment->number * 0x61C88647) & 0x1;
		break;
	}

	/* now that state has been determined, set the frame size */
	fragment->frame_width = ctxt->setup.size;
	fragment->frame_height = ctxt->setup.size;

	switch (ctxt->setup.dynamics) {
	case CHECKERS_DYNAMICS_ODD:
		break;
	case CHECKERS_DYNAMICS_EVEN:
		state = ~state & 0x1;
		break;
	case CHECKERS_DYNAMICS_ALTERNATING:
		state ^= ((unsigned)((float)ticks * ctxt->setup.rate) & 0x1);
		break;
	case CHECKERS_DYNAMICS_RANDOM: /* note: the big multiply here is just to get up out of the low bits */
		state &= hash(fragment->number * 0x61C88647 + (unsigned)((float)ticks * ctxt->setup.rate)) & 0x1;
		break;
	}

	if (fill == CHECKERS_FILL_RANDOM || fill == CHECKERS_FILL_MIXED)
		fill = rand_r(&ctxt->til_module_context.seed) % CHECKERS_FILL_RANDOM; /* TODO: mixed should have a setting for controlling the ratios */

	switch (ctxt->setup.fill) {
	case CHECKERS_FILL_SAMPLED:
		if (fragment->cleared)
			color = til_fb_fragment_get_pixel_unchecked(fragment, fragment->x + (fragment->width >> 1), fragment->y + (fragment->height >> 1));
		break;
	case CHECKERS_FILL_TEXTURED:
		flags = TIL_FB_DRAW_FLAG_TEXTURABLE;
		break;
	case CHECKERS_FILL_COLOR:
	default:
		break;
	}

	if (!state)
		til_fb_fragment_clear(fragment);
	else {
		if (!ctxt->setup.fill_module)
			til_fb_fragment_fill(fragment, flags, color);
		else {
			/* TODO: we need a way to send down color and flags, and use the module render as a brush of sorts */
			til_module_render(ctxt->fill_module_contexts[cpu], stream, ticks, fragment_ptr);
		}
	}
}


/* TODO: migrate to libtil */
static char * checkers_random_color(unsigned seed)
{
	/* til should probably have a common randomize color helper for this with a large collection of
	 * reasonable colors, and maybe even have themed palettes one can choose from... */
	static const char *	colors[] = {
					"#ffffff",
					"#ff0000",
					"#00ff00",
					"#0000ff",
					"#ffff00",
					"#00ffff",
					"#ff00ff",
				};

	return strdup(colors[seed % nelems(colors)]);
}


/* TODO: migrate to libtil */
static int checkers_rgb_to_uint32(const char *in, uint32_t *out)
{
	uint32_t	color = 0;

	/* this isn't html, but accept #rrggbb syntax */
	if (*in == '#')
		in++;
	else if (in[0] == '0' && in[1] == 'x') /* and 0xrrggbb */
		in += 2;

	if (strlen(in) != 6)
		return -EINVAL;

	for (int i = 0; i < 6;) {
		uint8_t	c = 0;

		color <<= 8;

		for (int j = 0; j < 2; in++, j++, i++) {
			c <<= 4;

			switch (*in) {
			case '0'...'9':
				c |= (*in) - '0';
				break;

			case 'a'...'f':
				c |= (*in) - 'a' + 10;
				break;

			case 'A'...'F':
				c |= (*in) - 'A' + 10;
				break;

			default:
				return -EINVAL;
			}
		}

		color |= c;
	}

	*out = color;

	return 0;
}


static int checkers_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*size;
	const char	*pattern;
	const char	*fill_module;
	const char	*dynamics;
	const char	*dynamics_rate;
	const char	*fill;
	const char	*color;
	const char	*size_values[] = {
				"4",
				"8",
				"16",
				"32",
				"64",
				"128",
				NULL
			};
	const char	*pattern_values[] = {
				"checkered",
				"random",
				NULL
			};
	const char	*fill_module_values[] = {
				"none",
				"blinds",
				"moire",
				"pixbounce",
				"plato",
				"roto",
				"shapes",
				"snow",
				"spiro",
				"stars",
				NULL
			};
	const char	*dynamics_values[] = {
				"odd",
				"even",
				"alternating",
				"random",
				NULL
			};
	const char	*dynamics_rate_values[] = {
				"1.0",
				".75",
				".5",
				".25",
				".1",
				".01",
				".001",
				".0001",
				NULL
			};
	const char	*fill_values[] = {
				"color",
				"sampled",
				"textured",
				"random",
				"mixed",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Checker size",
							.key = "size",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(CHECKERS_DEFAULT_SIZE),
							.values = size_values,
							.annotations = NULL
						},
						&size,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Checkers pattern",
							.key = "pattern",
							.preferred = pattern_values[0],
							.values = pattern_values,
							.annotations = NULL
						},
						&pattern,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Filled cell module (\"none\" for plain checkers)",
							.key = "fill_module",
							.preferred = fill_module_values[0],
							.values = fill_module_values,
							.annotations = NULL
						},
						&fill_module,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Checkers dynamics",
							.key = "dynamics",
							.preferred = dynamics_values[0],
							.values = dynamics_values,
							.annotations = NULL
						},
						&dynamics,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (strcasecmp(dynamics, "odd") && strcasecmp(dynamics, "even")) {
		r = til_settings_get_and_describe_value(settings,
							&(til_setting_desc_t){
								.name = "Checkers dynamics rate",
								.key = "dynamics_rate",
								.preferred = dynamics_rate_values[0],
								.values = dynamics_rate_values,
								.annotations = NULL
							},
							&dynamics_rate,
							res_setting,
							res_desc);
		if (r)
			return r;
	}

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Fill mode",
							.key = "fill",
							.preferred = fill_values[CHECKERS_DEFAULT_FILL],
							.values = fill_values,
							.annotations = NULL
						},
						&fill,
						res_setting,
						res_desc);
	if (r)
		return r;

	/* Even though sampled and textured fills don't neceesarily use the color,
	 * if there's no texture or no underlay to sample, we should have a color to fallback on.
	 */
	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Fill color",
							.key = "color",
							.preferred = TIL_SETTINGS_STR(CHECKERS_DEFAULT_COLOR),
							.random = checkers_random_color,
							.values = NULL,
							.annotations = NULL
						},
						&color,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		checkers_setup_t	*setup;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		sscanf(size, "%u", &setup->size);

		if (!strcasecmp(pattern, "checkered"))
			setup->pattern = CHECKERS_PATTERN_CHECKERED;
		else if (!strcasecmp(pattern, "random"))
			setup->pattern = CHECKERS_PATTERN_RANDOM;
		else {
			free(setup);
			return -EINVAL;
		}

		if (strcasecmp(fill_module, "none")) {
			setup->fill_module = til_lookup_module(fill_module);
			if (!setup->fill_module) {
				free(setup);
				return -ENOMEM;
			}
		}

		if (!strcasecmp(dynamics, "odd"))
			setup->dynamics = CHECKERS_DYNAMICS_ODD;
		else if (!strcasecmp(dynamics, "even"))
			setup->dynamics = CHECKERS_DYNAMICS_EVEN;
		else if (!strcasecmp(dynamics, "alternating"))
			setup->dynamics = CHECKERS_DYNAMICS_ALTERNATING;
		else if (!strcasecmp(dynamics, "random"))
			setup->dynamics = CHECKERS_DYNAMICS_RANDOM;
		else {
			free(setup);
			return -EINVAL;
		}

		if (setup->dynamics != CHECKERS_DYNAMICS_ODD && setup->dynamics != CHECKERS_DYNAMICS_EVEN)
			sscanf(dynamics_rate, "%f", &setup->rate);

		if (!strcasecmp(fill, "color"))
			setup->fill = CHECKERS_FILL_COLOR;
		else if (!strcasecmp(fill, "sampled"))
			setup->fill = CHECKERS_FILL_SAMPLED;
		else if (!strcasecmp(fill, "textured"))
			setup->fill = CHECKERS_FILL_TEXTURED;
		else if (!strcasecmp(fill, "random"))
			setup->fill = CHECKERS_FILL_RANDOM;
		else if (!strcasecmp(fill, "mixed"))
			setup->fill = CHECKERS_FILL_MIXED;
		else {
			free(setup);
			return -EINVAL;
		}

		r = checkers_rgb_to_uint32(color, &setup->color);
		if (r < 0)
			return r;

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	checkers_module = {
	.create_context = checkers_create_context,
	.destroy_context = checkers_destroy_context,
	.prepare_frame = checkers_prepare_frame,
	.render_fragment = checkers_render_fragment,
	.setup = checkers_setup,
	.name = "checkers",
	.description = "Checker-patterned overlay (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
