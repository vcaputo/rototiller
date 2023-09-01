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
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_stream.h"
#include "til_tap.h"

#define MOIRE_DEFAULT_CENTERS	2
#define MOIRE_DEFAULT_RINGS	20

typedef struct moire_setup_t {
	til_setup_t	til_setup;
	unsigned	n_centers;
	unsigned	n_rings;
} moire_setup_t;

typedef struct moire_center_t {
	float		x, y;
	float		seed;
	float		dir;
} moire_center_t;

typedef struct moire_context_t {
	til_module_context_t	til_module_context;
	moire_setup_t		*setup;

	struct {
		til_tap_t		n_rings;
	}			taps;

	struct {
		float			n_rings;
	}			vars;

	float			*n_rings;

	moire_center_t		centers[];
} moire_context_t;


static void moire_update_taps(moire_context_t *ctxt, til_stream_t *stream, unsigned ticks)
{
	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.n_rings))
		*ctxt->n_rings = ctxt->setup->n_rings;
	else
		ctxt->vars.n_rings = *ctxt->n_rings;

	if (ctxt->vars.n_rings <= 0.f)
		ctxt->vars.n_rings = 0.f;
}


static til_module_context_t * moire_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	moire_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(moire_context_t) + ((moire_setup_t *)setup)->n_centers * sizeof(moire_center_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->setup = (moire_setup_t *)setup;

	for (unsigned i = 0; i < ((moire_setup_t *)setup)->n_centers; i++) {
		ctxt->centers[i].seed = rand_r(&seed) * (1.f / (float)RAND_MAX) * 2 * M_PI;
		ctxt->centers[i].dir = (rand_r(&seed) * (2.f / (float)RAND_MAX) - 1.f);
		ctxt->centers[i].x = cosf(ctxt->centers[i].seed + (float)ticks * .001f * ctxt->centers[i].dir);
		ctxt->centers[i].y = sinf(ctxt->centers[i].seed + (float)ticks * .001f * ctxt->centers[i].dir);
	}

	ctxt->taps.n_rings = til_tap_init_float(ctxt, &ctxt->n_rings, 1, &ctxt->vars.n_rings, "n_rings");
	moire_update_taps(ctxt, stream, ticks);

	return &ctxt->til_module_context;
}


static void moire_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	moire_context_t	*ctxt = (moire_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu_x16 };

	moire_update_taps(ctxt, stream, ticks);

	for (unsigned i = 0; i < ctxt->setup->n_centers; i++) {
		ctxt->centers[i].x = cosf(ctxt->centers[i].seed + (float)ticks * .001f * ctxt->centers[i].dir);
		ctxt->centers[i].y = sinf(ctxt->centers[i].seed + (float)ticks * .001f * ctxt->centers[i].dir);
	}
}


static void moire_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	moire_context_t		*ctxt = (moire_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	float		xf = 2.f / (float)fragment->frame_width;
	float		yf = 2.f / (float)fragment->frame_height;
	unsigned	n_centers = ctxt->setup->n_centers;
	moire_center_t	*centers = ctxt->centers;
	float		n_rings = rintf(ctxt->vars.n_rings);
	float		cx, cy;

	/* TODO: optimize */
	cy = yf * (float)fragment->y - 1.f;
	for (int y = 0; y < fragment->height; y++, cy += yf) {

		cx = xf * (float)fragment->x - 1.f;
		for (int x = 0; x < fragment->width; x++, cx += xf) {
			unsigned char	filled = 0;

			for (unsigned i = 0; i < n_centers; i++) {
				float	dx, dy;

				dx = cx - centers[i].x;
				dy = cy - centers[i].y;

				filled ^= ((int)(sqrtf(dx * dx + dy * dy) * n_rings) & 0x1);
			}

			if (filled)
				til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, fragment->x + x, fragment->y + y, 0xffffffff);
			else if (!fragment->cleared)
				til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, 0x00000000);
		}
	}
}


static int moire_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	moire_module = {
	.create_context = moire_create_context,
	.prepare_frame = moire_prepare_frame,
	.render_fragment = moire_render_fragment,
	.setup = moire_setup,
	.name = "moire",
	.description = "2D Moire interference patterns (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


static int moire_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*centers;
	til_setting_t	*rings;
	const char	*centers_values[] = {
				"2",
				"3",
				"4",
				"5",
				NULL
			};
	const char	*rings_values[] = {
				"5",
				"10",
				"20",
				"40",
				"60",
				"80",
				"100",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Number of radial centers",
							.key = "centers",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(MOIRE_DEFAULT_CENTERS),
							.values = centers_values,
							.annotations = NULL
						},
						&centers,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Number of rings per center",
							.key = "rings",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(MOIRE_DEFAULT_RINGS),
							.values = rings_values,
							.annotations = NULL
						},
						&rings,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		moire_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &moire_module);
		if (!setup)
			return -ENOMEM;

		if (sscanf(centers->value, "%u", &setup->n_centers) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, centers, res_setting, -EINVAL);

		if (sscanf(rings->value, "%u", &setup->n_rings) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, rings, res_setting, -EINVAL);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
