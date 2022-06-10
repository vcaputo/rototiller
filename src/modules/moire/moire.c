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

#define MOIRE_DEFAULT_CENTERS	2

typedef struct moire_setup_t {
	til_setup_t	til_setup;
	unsigned	n_centers;
} moire_setup_t;

typedef struct moire_center_t {
	float		x, y;
	float		seed;
	float		dir;
} moire_center_t;

typedef struct moire_context_t {
	til_module_context_t	til_module_context;
	moire_setup_t		setup;
	moire_center_t		centers[];
} moire_context_t;

static moire_setup_t moire_default_setup = {
	.n_centers = MOIRE_DEFAULT_CENTERS,
};


static til_module_context_t * moire_create_context(unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	moire_context_t	*ctxt;

	if (!setup)
		setup = &moire_default_setup.til_setup;

	ctxt = til_module_context_new(sizeof(moire_context_t) + ((moire_setup_t *)setup)->n_centers * sizeof(moire_center_t), seed, ticks, n_cpus);
	if (!ctxt)
		return NULL;

	ctxt->setup = *(moire_setup_t *)setup;

	for (unsigned i = 0; i < ((moire_setup_t *)setup)->n_centers; i++) {
		ctxt->centers[i].seed = rand_r(&seed) * (1.f / (float)RAND_MAX) * 2 * M_PI;
		ctxt->centers[i].dir = (rand_r(&seed) * (2.f / (float)RAND_MAX) - 1.f);
		ctxt->centers[i].x = cosf(ctxt->centers[i].seed + (float)ticks * .001f * ctxt->centers[i].dir);
		ctxt->centers[i].y = sinf(ctxt->centers[i].seed + (float)ticks * .001f * ctxt->centers[i].dir);
	}

	return &ctxt->til_module_context;
}


static void moire_prepare_frame(til_module_context_t *context, unsigned ticks, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter)
{
	moire_context_t	*ctxt = (moire_context_t *)context;

	*res_fragmenter = til_fragmenter_slice_per_cpu;

	for (unsigned i = 0; i < ctxt->setup.n_centers; i++) {
		ctxt->centers[i].x = cosf(ctxt->centers[i].seed + (float)ticks * .001f * ctxt->centers[i].dir);
		ctxt->centers[i].y = sinf(ctxt->centers[i].seed + (float)ticks * .001f * ctxt->centers[i].dir);
	}
}


static void moire_render_fragment(til_module_context_t *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	moire_context_t	*ctxt = (moire_context_t *)context;
	float		xf = 2.f / (float)fragment->frame_width;
	float		yf = 2.f / (float)fragment->frame_height;
	float		cx, cy;

	/* TODO: optimize */
	cy = yf * (float)fragment->y - 1.f;
	for (int y = fragment->y; y < fragment->y + fragment->height; y++, cy += yf) {

		cx = xf * (float)fragment->x - 1.f;
		for (int x = fragment->x; x < fragment->x + fragment->width; x++, cx += xf) {
			int	filled = 0;

			for (unsigned i = 0; i < ctxt->setup.n_centers; i++) {
				float	dx, dy;

				dx = cx - ctxt->centers[i].x;
				dy = cy - ctxt->centers[i].y;

				if (cosf(sqrtf(dx * dx + dy * dy) * 50.f) < 0.f)
					filled ^= 1;
			}

			if (filled)
				til_fb_fragment_put_pixel_unchecked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x, y, 0xffffffff);
			else if (!fragment->cleared)
				til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, 0x00000000);
		}
	}
}


static int moire_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*centers;
	const char	*values[] = {
				"2",
				"3",
				"4",
				"5",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Number of radial centers",
							.key = "centers",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(MOIRE_DEFAULT_CENTERS),
							.values = values,
							.annotations = NULL
						},
						&centers,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		moire_setup_t	*setup;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		sscanf(centers, "%u", &setup->n_centers);

		*res_setup = &setup->til_setup;
	}

	return 0;
}


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
