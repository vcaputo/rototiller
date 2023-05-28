#include <errno.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_util.h"

#include "v2f.h"

/* Rudimentary Voronoi diagram module:
 * https://en.wikipedia.org/wiki/Voronoi_diagram
 *
 * When used as an overlay, the output fragment's contents are sampled for
 * coloring the cells producing a realtime mosaic style effect.
 */

/* Copyright (C) 2022 Vito Caputo <vcaputo@pengaru.com> */

typedef struct voronoi_setup_t {
	til_setup_t		til_setup;
	size_t			n_cells;
	unsigned		randomize:1;
} voronoi_setup_t;

typedef struct voronoi_cell_t {
	v2f_t			origin;
	uint32_t		color;
} voronoi_cell_t;

typedef struct voronoi_distance_t {
	voronoi_cell_t		*cell;
	float			distance_sq;
} voronoi_distance_t;

typedef struct voronoi_distances_t {
	int			width, height;
	size_t			size;
	voronoi_distance_t	*buf;
} voronoi_distances_t;

typedef struct voronoi_context_t {
	til_module_context_t	til_module_context;
	unsigned		seed;
	voronoi_setup_t		*setup;
	voronoi_distances_t	distances;
	voronoi_cell_t		cells[];
} voronoi_context_t;


#define VORONOI_DEFAULT_N_CELLS		1024
#define VORONOI_DEFAULT_DIRTY		0
#define VORONOI_DEFAULT_RANDOMIZE	0


static void voronoi_randomize(voronoi_context_t *ctxt)
{
	float	inv_rand_max= 1.f / (float)RAND_MAX;

	for (size_t i = 0; i < ctxt->setup->n_cells; i++) {
		voronoi_cell_t	*p = &ctxt->cells[i];

		p->origin.x = ((float)rand_r(&ctxt->seed) * inv_rand_max) * 2.f - 1.f;
		p->origin.y = ((float)rand_r(&ctxt->seed) * inv_rand_max) * 2.f - 1.f;

		p->color = ((uint32_t)(rand_r(&ctxt->seed) % 256)) << 16;
		p->color |= ((uint32_t)(rand_r(&ctxt->seed) % 256)) << 8;
		p->color |= ((uint32_t)(rand_r(&ctxt->seed) % 256));
	}
}


static til_module_context_t * voronoi_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	voronoi_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(voronoi_context_t) + ((voronoi_setup_t *)setup)->n_cells * sizeof(voronoi_cell_t), stream, seed, ticks, n_cpus, path, setup);
	if (!ctxt)
		return NULL;

	ctxt->setup = (voronoi_setup_t *)setup;
	ctxt->seed = seed;

	voronoi_randomize(ctxt);

	return &ctxt->til_module_context;
}


static void voronoi_destroy_context(til_module_context_t *context)
{
	voronoi_context_t	*ctxt = (voronoi_context_t *)context;

	free(ctxt->distances.buf);
	free(ctxt);
}


static inline size_t voronoi_cell_origin_to_distance_idx(const voronoi_context_t *ctxt, const voronoi_cell_t *cell)
{
	size_t	x, y;

	x = (cell->origin.x * .5f + .5f) * (float)(ctxt->distances.width - 1);
	y = (cell->origin.y * .5f + .5f) * (float)(ctxt->distances.height - 1);

	return y * ctxt->distances.width + x;
}


static void voronoi_jumpfill_pass(voronoi_context_t *ctxt, v2f_t *ds, size_t step)
{
	voronoi_distance_t	*d = ctxt->distances.buf;
	v2f_t			dp = {};

	dp.y = -1.f;
	for (int y = 0; y < ctxt->distances.height; y++, dp.y += ds->y) {

		dp.x = -1.f;
		for (int x = 0; x < ctxt->distances.width; x++, dp.x += ds->x, d++) {
			voronoi_distance_t	*dq;

			if (d->cell && d->distance_sq == 0)
				continue;

#define VORONOI_JUMPFILL																\
				if (dq->cell) {														\
					float	dist_sq = v2f_distance_sq(&dq->cell->origin, &dp);							\
																			\
					if (!d->cell) { /* we're unassigned, just join dq's cell */							\
						d->cell = dq->cell;											\
						d->distance_sq = dist_sq;										\
					} else if (dist_sq < d->distance_sq) { /* is dq's cell's origin closer than the present one?  then join it */	\
						d->cell = dq->cell;											\
						d->distance_sq = dist_sq;										\
					}														\
				}

			if (x >= step) {
				/* can sample to the left */
				dq = d - step;

				VORONOI_JUMPFILL;

				if (y >= step) {
					/* can sample above and to the left */
					dq = d - step * ctxt->distances.width - step;

					VORONOI_JUMPFILL;
				}

				if (ctxt->distances.height - y > step) {
					/* can sample below and to the left */
					dq = d + step * ctxt->distances.width - step;

					VORONOI_JUMPFILL;
				}

			}

			if (ctxt->distances.width - x > step) {
				/* can sample to the right */
				dq = d + step;

				VORONOI_JUMPFILL;

				if (y >= step) {
					/* can sample above and to the right */
					dq = d - step * ctxt->distances.width + step;

					VORONOI_JUMPFILL;
				}

				if (ctxt->distances.height - y > step) {
					/* can sample below */
					dq = d + step * ctxt->distances.width + step;

					VORONOI_JUMPFILL;
				}
			}

			if (y >= step) {
				/* can sample above */
				dq = d - step * ctxt->distances.width;

				VORONOI_JUMPFILL;
			}

			if (ctxt->distances.height - y > step) {
				/* can sample below */
				dq = d + step * ctxt->distances.width;

				VORONOI_JUMPFILL;
			}
		}
	}
}


static void voronoi_calculate_distances(voronoi_context_t *ctxt)
{
	v2f_t	ds = (v2f_t){
			.x = 2.f / ctxt->distances.width,
			.y = 2.f / ctxt->distances.height,
		};

	memset(ctxt->distances.buf, 0, ctxt->distances.size * sizeof(*ctxt->distances.buf));

#if 0
	/* naive inefficient brute-force but correct algorithm */
	for (size_t i = 0; i < ctxt->setup->n_cells; i++) {
		voronoi_distance_t	*d = ctxt->distances.buf;
		v2f_t			dp = {};

		dp.y = -1.f;
		for (int y = 0; y < ctxt->distances.height; y++, dp.y += ds.y) {

			dp.x = -1.f;
			for (int x = 0; x < ctxt->distances.width; x++, dp.x += ds.x, d++) {
				float	dist_sq;

				dist_sq = v2f_distance_sq(&ctxt->cells[i].origin, &dp);
				if (!d->cell || dist_sq < d->distance_sq) {
					d->cell = &ctxt->cells[i];
					d->distance_sq = dist_sq;
				}
			}
		}
	}
#else
	/* An attempt at implementing https://en.wikipedia.org/wiki/Jump_flooding_algorithm */

	/* first assign the obvious zero-distance cell origins */
	for (size_t i = 0; i < ctxt->setup->n_cells; i++) {
		voronoi_cell_t		*c = &ctxt->cells[i];
		size_t			idx;
		voronoi_distance_t	*d;

		idx = voronoi_cell_origin_to_distance_idx(ctxt, c);
		d = &ctxt->distances.buf[idx];

		d->cell = c;
		d->distance_sq = 0.f;
	}

	/* now for every distance sample neighbors */
	for (size_t step = MAX(ctxt->distances.width, ctxt->distances.height) / 2; step > 0; step >>= 1)
		voronoi_jumpfill_pass(ctxt, &ds, step);
#endif
}


static void voronoi_sample_colors(voronoi_context_t *ctxt, const til_fb_fragment_t *fragment)
{
	for (size_t i = 0; i < ctxt->setup->n_cells; i++) {
		voronoi_cell_t	*p = &ctxt->cells[i];
		int		x, y;

		x = (p->origin.x * .5f + .5f) * (fragment->frame_width - 1);
		y = (p->origin.y * .5f + .5f) * (fragment->frame_height - 1);

		p->color = fragment->buf[y * fragment->pitch + x];
	}
}


static void voronoi_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	voronoi_context_t	*ctxt = (voronoi_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_tile64 };

	if (!ctxt->distances.buf ||
	    ctxt->distances.width != fragment->frame_width ||
	    ctxt->distances.height != fragment->frame_height) {

		free(ctxt->distances.buf);
		ctxt->distances.width = fragment->frame_width;
		ctxt->distances.height = fragment->frame_height;
		ctxt->distances.size = fragment->frame_width * fragment->frame_height;
		ctxt->distances.buf = malloc(sizeof(voronoi_distance_t) * ctxt->distances.size);

		if (!ctxt->setup->randomize)
			voronoi_calculate_distances(ctxt);
	}

	/* TODO: explore moving voronoi_calculate_distances() into render_fragment (threaded) */

	if (ctxt->setup->randomize) {
		voronoi_randomize(ctxt);
		voronoi_calculate_distances(ctxt);
	}

	/* if the fragment comes in already cleared/initialized, use it for the colors, producing a mosaic */
	if (fragment->cleared)
		voronoi_sample_colors(ctxt, fragment);
}


static void voronoi_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	voronoi_context_t	*ctxt = (voronoi_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	for (int y = 0; y < fragment->height; y++) {
		for (int x = 0; x < fragment->width; x++) {
			fragment->buf[y * fragment->pitch + x] = ctxt->distances.buf[(y + fragment->y) * ctxt->distances.width + (fragment->x + x)].cell->color;
		}
	}
}


static int voronoi_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*n_cells;
	const char	*n_cells_values[] = {
				"512",
				"1024",
				"2048",
				"4096",
				"8192",
				"16384",
				"32768",
				NULL
			};
	const char	*randomize;
	const char	*bool_values[] = {
				"off",
				"on",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Voronoi cells quantity",
							.key = "cells",
							.regex = "^[0-9]+",
							.preferred = TIL_SETTINGS_STR(VORONOI_DEFAULT_N_CELLS),
							.values = n_cells_values,
							.annotations = NULL
						},
						&n_cells,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Constantly randomize cell placement",
							.key = "randomize",
							.regex = "^(on|off)",
							.preferred = bool_values[VORONOI_DEFAULT_RANDOMIZE],
							.values = bool_values,
							.annotations = NULL
						},
						&randomize,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		voronoi_setup_t	*setup;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		sscanf(n_cells, "%zu", &setup->n_cells);

		if (!strcasecmp(randomize, "on"))
			setup->randomize = 1;

		*res_setup = &setup->til_setup;
	}
	return 0;
}


til_module_t	voronoi_module = {
	.create_context = voronoi_create_context,
	.destroy_context = voronoi_destroy_context,
	.prepare_frame = voronoi_prepare_frame,
	.render_fragment = voronoi_render_fragment,
	.setup = voronoi_setup,
	.name = "voronoi",
	.description = "Voronoi diagram",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
