#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "til.h"
#include "til_fb.h"

/* Copyright (C) 2017-2022 Vito Caputo <vcaputo@pengaru.com> */

#define	BLINDS_DEFAULT_COUNT		16
#define BLINDS_DEFAULT_ORIENTATION	BLINDS_ORIENTATION_HORIZONTAL


typedef enum blinds_orientation_t {
	BLINDS_ORIENTATION_HORIZONTAL,
	BLINDS_ORIENTATION_VERTICAL,
} blinds_orientation_t;

typedef struct blinds_setup_t {
	til_setup_t		til_setup;
	unsigned		count;
	blinds_orientation_t	orientation;
} blinds_setup_t;

typedef struct blinds_context_t {
	blinds_setup_t	setup;
} blinds_context_t;


static blinds_setup_t blinds_default_setup = {
	.count = BLINDS_DEFAULT_COUNT,
	.orientation = BLINDS_DEFAULT_ORIENTATION,
};


static void * blinds_create_context(unsigned ticks, unsigned num_cpus, til_setup_t *setup)
{
	blinds_context_t	*ctxt;

	if (!setup)
		setup = &blinds_default_setup.til_setup;

	ctxt = calloc(1, sizeof(blinds_context_t));
	if (!ctxt)
		return NULL;

	ctxt->setup = *(blinds_setup_t *)setup;

	return ctxt;
}


static void blinds_destroy_context(void *context)
{
	blinds_context_t	*ctxt = context;

	free(ctxt);
}


/* draw a horizontal blind over fragment */
static inline void draw_blind_horizontal(til_fb_fragment_t *fragment, unsigned row, unsigned count, float t)
{
	unsigned	row_height = fragment->frame_height / count;
	unsigned	height = roundf(t * (float)row_height);

	for (unsigned y = 0; y < height; y++)
		memset(fragment->buf + ((row * row_height) + y ) * (fragment->pitch >> 2), 0xff, fragment->width * 4);
}


/* draw a vertical blind over fragment */
static inline void draw_blind_vertical(til_fb_fragment_t *fragment, unsigned column, unsigned count, float t)
{
	unsigned	column_width = fragment->frame_width / count;
	unsigned	width = roundf(t * (float)column_width);

	for (unsigned y = 0; y < fragment->height; y++)
		memset(fragment->buf + y * (fragment->pitch >> 2) + column * column_width, 0xff, width * 4);
}


/* draw blinds over the fragment */
static void blinds_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	blinds_context_t	*ctxt = context;

	static float rr;

	unsigned	blind;
	float		r;

	til_fb_fragment_clear(fragment);

	for (r = rr, blind = 0; blind < ctxt->setup.count; blind++, r += .1) {
		switch (ctxt->setup.orientation) {
		case BLINDS_ORIENTATION_HORIZONTAL:
			draw_blind_horizontal(fragment, blind, ctxt->setup.count, 1.f - fabsf(cosf(r)));
			break;
		case BLINDS_ORIENTATION_VERTICAL:
			draw_blind_vertical(fragment, blind, ctxt->setup.count, 1.f - fabsf(cosf(r)));
			break;
		}
	}

	rr += .01;
}


static int blinds_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*orientation;
	const char	*count;
	const char	*orientation_values[] = {
				"horizontal",
				"vertical",
				NULL
			};
	const char	*count_values[] = {
				"2",
				"4",
				"8",
				"12",
				"16",
				"24",
				"32",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Blinds orientation",
							.key = "orientation",
							.regex = "^(horizontal|vertical)",
							.preferred = orientation_values[BLINDS_DEFAULT_ORIENTATION],
							.values = orientation_values,
							.annotations = NULL
						},
						&orientation,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Blinds count",
							.key = "count",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(BLINDS_DEFAULT_COUNT),
							.values = count_values,
							.annotations = NULL
						},
						&count,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		blinds_setup_t	*setup;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		sscanf(count, "%u", &setup->count);

		if (!strcasecmp(orientation, "horizontal")) {
			setup->orientation = BLINDS_ORIENTATION_HORIZONTAL;
		} else if (!strcasecmp(orientation, "vertical")) {
			setup->orientation = BLINDS_ORIENTATION_VERTICAL;
		} else {
			til_setup_free(&setup->til_setup);

			return -EINVAL;
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	blinds_module = {
	.create_context = blinds_create_context,
	.destroy_context = blinds_destroy_context,
	.render_fragment = blinds_render_fragment,
	.setup = blinds_setup,
	.name = "blinds",
	.description = "Retro 80s-inspired window blinds",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
