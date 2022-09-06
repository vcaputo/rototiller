#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

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
	til_module_context_t	til_module_context;
	blinds_setup_t		setup;
} blinds_context_t;


static blinds_setup_t blinds_default_setup = {
	.count = BLINDS_DEFAULT_COUNT,
	.orientation = BLINDS_DEFAULT_ORIENTATION,
};


static til_module_context_t * blinds_create_context(unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	blinds_context_t	*ctxt;

	if (!setup)
		setup = &blinds_default_setup.til_setup;

	ctxt = til_module_context_new(sizeof(blinds_context_t), seed, ticks, n_cpus);
	if (!ctxt)
		return NULL;

	ctxt->setup = *(blinds_setup_t *)setup;

	return &ctxt->til_module_context;
}


/* draw a horizontal blind over fragment */
static inline void draw_blind_horizontal(til_fb_fragment_t *fragment, unsigned row, unsigned count, float t)
{
	unsigned	row_height = fragment->frame_height / count;
	unsigned	height = roundf(t * (float)row_height);

/* XXX FIXME: use faster block style fill/copy if til_fb gets that */
	for (unsigned y = 0; y < height; y++) {
		for (unsigned x = 0; x < fragment->width; x++)
			til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, fragment->x + x, fragment->y + y + row * row_height, 0xffffffff); /* FIXME: use _unchecked() variant, but must not assume frame_height == height */
	}
}


/* draw a vertical blind over fragment */
static inline void draw_blind_vertical(til_fb_fragment_t *fragment, unsigned column, unsigned count, float t)
{
	unsigned	column_width = fragment->frame_width / count;
	unsigned	width = roundf(t * (float)column_width);

/* XXX FIXME: use faster block style fill/copy if til_fb gets that */
	for (unsigned y = 0; y < fragment->height; y++) {
		for (unsigned x = 0; x < width; x++)
			til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, fragment->x + x + column * column_width, fragment->y + y, 0xffffffff); /* FIXME: use _unchecked() variant, but must not assume frame_width == width */
	}
}


/* draw blinds over the fragment */
static void blinds_render_fragment(til_module_context_t *context, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	blinds_context_t	*ctxt = (blinds_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	float		r = til_ticks_to_rads(ticks);
	unsigned	blind;

	til_fb_fragment_clear(fragment);

	for (blind = 0; blind < ctxt->setup.count; blind++, r += .1) {
		switch (ctxt->setup.orientation) {
		case BLINDS_ORIENTATION_HORIZONTAL:
			draw_blind_horizontal(fragment, blind, ctxt->setup.count, 1.f - fabsf(cosf(r)));
			break;
		case BLINDS_ORIENTATION_VERTICAL:
			draw_blind_vertical(fragment, blind, ctxt->setup.count, 1.f - fabsf(cosf(r)));
			break;
		}
	}
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
	.render_fragment = blinds_render_fragment,
	.setup = blinds_setup,
	.name = "blinds",
	.description = "Retro 80s-inspired window blinds",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
