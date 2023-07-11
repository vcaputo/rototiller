#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_stream.h"
#include "til_tap.h"

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
	struct {
		til_tap_t	t, step, count;
	} taps;

	struct {
		float		t, step, count;
	} vars;

	float			*t, *step, *count;
	blinds_setup_t		*setup;
} blinds_context_t;


static void blinds_update_taps(blinds_context_t *ctxt, til_stream_t *stream, unsigned ticks)
{
	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.t))
		*ctxt->t = til_ticks_to_rads(ticks);

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.step))
		*ctxt->step = .1f;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.count))
		*ctxt->count = ctxt->setup->count;
}


static til_module_context_t * blinds_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	blinds_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(blinds_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->taps.t = til_tap_init_float(ctxt, &ctxt->t, 1, &ctxt->vars.t, "T");
	ctxt->taps.step = til_tap_init_float(ctxt, &ctxt->step, 1, &ctxt->vars.step, "step");
	ctxt->taps.count = til_tap_init_float(ctxt, &ctxt->count, 1, &ctxt->vars.count, "count");

	ctxt->setup = (blinds_setup_t *)setup;

	blinds_update_taps(ctxt, stream, ticks);

	return &ctxt->til_module_context;
}


static void blinds_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_tile64 };
}


/* draw a horizontal blind over fragment */
static inline void draw_blind_horizontal(til_fb_fragment_t *fragment, unsigned row, unsigned count, float t)
{
	float		row_height = fragment->frame_height / (float)count;
	unsigned	height = roundf(t * row_height);
	unsigned	row_y = row * row_height;

	if (row_y >= fragment->y + fragment->height)
		return;

	if (row_y + height <= fragment->y)
		return;

	{
		unsigned	ystart = MAX(row_y, fragment->y);
		unsigned	yend = MIN(row_y + height, fragment->y + fragment->height);
		unsigned	xstart = fragment->x;
		unsigned	xend = fragment->x + fragment->width;

		for (unsigned y = ystart; y < yend; y++) {
			/* XXX FIXME: use faster block style fill/copy if til_fb gets that */
			for (unsigned x = xstart; x < xend; x++) {
				til_fb_fragment_put_pixel_unchecked(fragment,
								    TIL_FB_DRAW_FLAG_TEXTURABLE,
								    x, y, 0xffffffff);
			}
		}
	}
}


/* draw a vertical blind over fragment */
static inline void draw_blind_vertical(til_fb_fragment_t *fragment, unsigned column, unsigned count, float t)
{
	float		column_width = fragment->frame_width / (float)count;
	unsigned	width = roundf(t * column_width);
	unsigned	column_x = column * column_width;

	if (column_x >= fragment->x + fragment->width)
		return;

	if (column_x + width <= fragment->x)
		return;

	{
		unsigned	xstart = MAX(column_x, fragment->x);
		unsigned	xend = MIN(column_x + width, fragment->x + fragment->width);
		unsigned	ystart = fragment->y;
		unsigned	yend = fragment->y + fragment->height;

		for (unsigned y = ystart; y < yend; y++) {
			/* XXX FIXME: use faster block style fill/copy if/when til_fb gets that */
			for (unsigned x = xstart; x < xend; x++) {
				til_fb_fragment_put_pixel_unchecked(fragment,
								    TIL_FB_DRAW_FLAG_TEXTURABLE,
								    x, y, 0xffffffff);
			}
		}
	}
}


/* draw blinds over the fragment */
static void blinds_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	blinds_context_t	*ctxt = (blinds_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	unsigned	blind;
	float		t;

	blinds_update_taps(ctxt, stream, ticks);

	til_fb_fragment_clear(fragment);

	for (t = *ctxt->t, blind = 0; blind < (unsigned)*ctxt->count; blind++, t += *ctxt->step) {
		switch (ctxt->setup->orientation) {
		case BLINDS_ORIENTATION_HORIZONTAL:
			draw_blind_horizontal(fragment, blind, ctxt->setup->count, 1.f - fabsf(cosf(t)));
			break;
		case BLINDS_ORIENTATION_VERTICAL:
			draw_blind_vertical(fragment, blind, ctxt->setup->count, 1.f - fabsf(cosf(t)));
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
						&(til_setting_spec_t){
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
						&(til_setting_spec_t){
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

		setup = til_setup_new(settings, sizeof(*setup), NULL);
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
	.prepare_frame = blinds_prepare_frame,
	.render_fragment = blinds_render_fragment,
	.setup = blinds_setup,
	.name = "blinds",
	.description = "Retro 80s-inspired window blinds (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
