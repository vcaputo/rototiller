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


#define CHECKERS_DEFAULT_SIZE		32
#define CHECKERS_DEFAULT_PATTERN	CHECKERS_PATTERN_CHECKERED
#define CHECKERS_DEFAULT_DYNAMICS	CHECKERS_DYNAMICS_ODD
#define CHECKERS_DEFAULT_DYNAMICS_RATE	1.0


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

typedef struct checkers_setup_t {
	til_setup_t		til_setup;
	unsigned		size;
	checkers_pattern_t	pattern;
	checkers_dynamics_t	dynamics;
	float			rate;
} checkers_setup_t;

typedef struct checkers_context_t {
	checkers_setup_t	setup;
} checkers_context_t;


static checkers_setup_t checkers_default_setup = {
	.size = CHECKERS_DEFAULT_SIZE,
	.pattern = CHECKERS_DEFAULT_PATTERN,
	.dynamics = CHECKERS_DEFAULT_DYNAMICS,
	.rate = CHECKERS_DEFAULT_DYNAMICS_RATE,
};


static void * checkers_create_context(unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	checkers_context_t	*ctxt;

	if (!setup)
		setup = &checkers_default_setup.til_setup;

	ctxt = calloc(1, sizeof(checkers_context_t));
	if (!ctxt)
		return NULL;

	ctxt->setup = *(checkers_setup_t *)setup;

	return ctxt;
}


static void checkers_destroy_context(void *context)
{
	free(context);
}


static int checkers_fragmenter(void *context, unsigned n_cpus, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	checkers_context_t	*ctxt = context;

	return til_fb_fragment_tile_single(fragment, ctxt->setup.size, number, res_fragment);
}


static void checkers_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter)
{
	*res_fragmenter = checkers_fragmenter;
}


static inline unsigned hash(unsigned x)
{
	x = ((x >> 16) ^ x) * 0x61C88647;
	x = ((x >> 16) ^ x) * 0x61C88647;
	x = ((x >> 16) ^ x) * 0x61C88647;
	x = (x >> 16) ^ x;

	return x;
}


static void checkers_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	checkers_context_t	*ctxt = context;
	int			state;

	switch (ctxt->setup.pattern) {
	case CHECKERS_PATTERN_CHECKERED: {
		unsigned	tiles_per_row;

		tiles_per_row = fragment->frame_width / ctxt->setup.size;
		if (tiles_per_row * ctxt->setup.size < fragment->frame_width)
			tiles_per_row++;

		state = (fragment->number + (fragment->y / ctxt->setup.size) * !(tiles_per_row & 0x1)) & 0x1;
		break;
	}
	case CHECKERS_PATTERN_RANDOM:
		state = hash(fragment->number * 0x61C88647) & 0x1;
		break;
	}

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

	if (!state)
		til_fb_fragment_clear(fragment);
	else
		til_fb_fragment_fill(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, 0xffffffff);
}


static int checkers_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*size;
	const char	*pattern;
	const char	*dynamics;
	const char	*dynamics_rate;
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

	if (res_setup) {
		checkers_setup_t	*setup;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		sscanf(size, "%u", &setup->size);

		if (!strcmp(pattern, "checkered"))
			setup->pattern = CHECKERS_PATTERN_CHECKERED;
		else if (!strcmp(pattern, "random"))
			setup->pattern = CHECKERS_PATTERN_RANDOM;
		else {
			free(setup);
			return -EINVAL;
		}

		if (!strcmp(dynamics, "odd"))
			setup->dynamics = CHECKERS_DYNAMICS_ODD;
		else if (!strcmp(dynamics, "even"))
			setup->dynamics = CHECKERS_DYNAMICS_EVEN;
		else if (!strcmp(dynamics, "alternating"))
			setup->dynamics = CHECKERS_DYNAMICS_ALTERNATING;
		else if (!strcmp(dynamics, "random"))
			setup->dynamics = CHECKERS_DYNAMICS_RANDOM;
		else {
			free(setup);
			return -EINVAL;
		}

		if (setup->dynamics != CHECKERS_DYNAMICS_ODD && setup->dynamics != CHECKERS_DYNAMICS_EVEN)
			sscanf(dynamics_rate, "%f", &setup->rate);

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
