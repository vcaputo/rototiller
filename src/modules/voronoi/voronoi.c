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
	unsigned		recalc_needed:1;
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


/* TODO: stuff like this makes me think there needs to be support for threaded prepare_frame(),
 * since this could just have per-cpu lists of cells and per-cpu rand_r seeds which could make
 * a significant difference for large numbers of cells.
 */
static void voronoi_randomize(voronoi_context_t *ctxt, int do_colors)
{
	float	inv_rand_max= 1.f / (float)RAND_MAX;

	if (!do_colors) {
		/* we can skip setting the colors when overlayed since they get sampled */
		for (size_t i = 0; i < ctxt->setup->n_cells; i++) {
			voronoi_cell_t	*p = &ctxt->cells[i];

			p->origin.x = ((float)rand_r(&ctxt->seed) * inv_rand_max) * 2.f - 1.f;
			p->origin.y = ((float)rand_r(&ctxt->seed) * inv_rand_max) * 2.f - 1.f;
		}
	} else {
		for (size_t i = 0; i < ctxt->setup->n_cells; i++) {
			voronoi_cell_t	*p = &ctxt->cells[i];

			p->origin.x = ((float)rand_r(&ctxt->seed) * inv_rand_max) * 2.f - 1.f;
			p->origin.y = ((float)rand_r(&ctxt->seed) * inv_rand_max) * 2.f - 1.f;

			p->color = ((uint32_t)(rand_r(&ctxt->seed) % 256)) << 16;
			p->color |= ((uint32_t)(rand_r(&ctxt->seed) % 256)) << 8;
			p->color |= ((uint32_t)(rand_r(&ctxt->seed) % 256));
		}
	}

	ctxt->distances.recalc_needed = 1;
}


static til_module_context_t * voronoi_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	voronoi_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(voronoi_context_t) + ((voronoi_setup_t *)setup)->n_cells * sizeof(voronoi_cell_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->setup = (voronoi_setup_t *)setup;
	ctxt->seed = seed;

	voronoi_randomize(ctxt, 1);

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


static size_t voronoi_jumpfill_pass(voronoi_context_t *ctxt, const v2f_t *db, const v2f_t *ds, size_t step, const til_fb_fragment_t *fragment)
{
	size_t	n_unassigned = 0;
	v2f_t	dp = {};

	dp.y = db->y;
	for (int y = 0; y < fragment->height; y++, dp.y += ds->y) {
		voronoi_distance_t	*d = &ctxt->distances.buf[(fragment->y + y) * ctxt->distances.width + fragment->x];

		dp.x = db->x;
		for (int x = 0; x < fragment->width; x++, dp.x += ds->x, d++) {
			voronoi_distance_t	*dq;

			if (d->cell && d->distance_sq == 0)
				continue;

			/* FIXME TODO: this almost certainly needs to use some atomics or at least more care in dereferencing dq->cell and
			 * writing to d->cell, since we perform jumpfill concurrently in render_fragment, and the step range deliberately
			 * puts us outside the current fragment's boundaries.
			 */
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

			if (fragment->x + x >= step) {
				/* can sample to the left */
				dq = d - step;

				VORONOI_JUMPFILL;

				if (fragment->y + y >= step) {
					/* can sample above and to the left */
					dq = d - step * ctxt->distances.width - step;

					VORONOI_JUMPFILL;
				}

				if (fragment->frame_height - (fragment->y + y) > step) {
					/* can sample below and to the left */
					dq = d + step * ctxt->distances.width - step;

					VORONOI_JUMPFILL;
				}

			}

			if (fragment->frame_width - (fragment->x + x) > step) {
				/* can sample to the right */
				dq = d + step;

				VORONOI_JUMPFILL;

				if (fragment->y + y >= step) {
					/* can sample above and to the right */
					dq = d - step * ctxt->distances.width + step;

					VORONOI_JUMPFILL;
				}

				if (fragment->frame_height - (fragment->y + y) > step) {
					/* can sample below */
					dq = d + step * ctxt->distances.width + step;

					VORONOI_JUMPFILL;
				}
			}

			if (fragment->y + y >= step) {
				/* can sample above */
				dq = d - step * ctxt->distances.width;

				VORONOI_JUMPFILL;
			}

			if (fragment->frame_height - (fragment->y + y) > step) {
				/* can sample below */
				dq = d + step * ctxt->distances.width;

				VORONOI_JUMPFILL;
			}

			if (!d->cell)
				n_unassigned++;
		}
	}

	return n_unassigned;
}


/* distance calculating is split into two halves:
 * 1. a serialized global/cell-oriented part, where the distances are wholesale
 *    reset then the "seeds" placed according to the cells.
 * 2. a concurrent distance-oriented part, where per-pixel distances are computed
 *    within the bounds of the supplied fragment (tiled)
 *
 * These occur in prepare_pass/render_pass, respectively.
 */
static void voronoi_calculate_distances_prepare_pass(voronoi_context_t *ctxt)
{
	memset(ctxt->distances.buf, 0, ctxt->distances.size * sizeof(*ctxt->distances.buf));

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
}


static void voronoi_calculate_distances_render_pass(voronoi_context_t *ctxt, const til_fb_fragment_t *fragment)
{
	v2f_t	ds = (v2f_t){
			.x = 2.f / fragment->frame_width,
			.y = 2.f / fragment->frame_height,
		};
	v2f_t	db = (v2f_t){
			.x = fragment->x * ds.x - 1.f,
			.y = fragment->y * ds.y - 1.f,
		};
	size_t	n_unassigned;

	/* An attempt at implementing https://en.wikipedia.org/wiki/Jump_flooding_algorithm */

	/* now for every distance sample neighbors */

	/* The step range still has to access the entire frame to ensure we can still find "seed" cells
	 * even when the current fragment/tile doesn't encompass any of them.
	 *
	 * i.e. if we strictly sampled within our fragment's bounds, we'd potentially not find a seed cell
	 * at all - epsecially in scenarios having small numbers of cells relative to the number of fragments/tiles.
	 *
	 * But aside from the potentially-missed-seed-cell bug, staying strictly without our fragment's
	 * boundaries for sampling also would result in clearly visible tile edges in the diagram.
	 *
	 * So no, we can't just treat every fragment as its own little isolated distances buf within the greater
	 * one.  This does make it more complicated since outside our fragment's bounds other threads may be
	 * changing the cell pointers while we try dereference them.  But all we really care about is finding
	 * seeds reliably, and those should already be populated in the prepare phase.
	 */
	do {
		for (size_t step = MAX(fragment->frame_width, fragment->frame_height) >> 1; step > 0; step >>= 1)
			n_unassigned = voronoi_jumpfill_pass(ctxt, &db, &ds, step, fragment);
	} while (n_unassigned); /* FIXME: there seems to be bug/race potential with sparse voronois at high res,
				 * especially w/randomize=on where jumpfill constantly recurs, it could leave a
				 * spurious NULL cell resulting in a segfault.  The simplest thing to do here is
				 * just repeat the jumpfill for the fragment.  It's inefficient, but rare, and doing
				 * voronoi as-is that way on a high resolution is brutally slow anyways, this all needs
				 * revisiting to make things scale better.  So for now this prevents crashing, which is
				 * all that matters.
				 */
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
		ctxt->distances.recalc_needed = 1;
	}

	if (ctxt->setup->randomize)
		voronoi_randomize(ctxt, !fragment->cleared);

	/* if the fragment comes in already cleared/initialized, use it for the colors, producing a mosaic */
	if (fragment->cleared)
		voronoi_sample_colors(ctxt, fragment);

	if (ctxt->distances.recalc_needed)
		voronoi_calculate_distances_prepare_pass(ctxt);
}


static void voronoi_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	voronoi_context_t	*ctxt = (voronoi_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	if (ctxt->distances.recalc_needed)
		voronoi_calculate_distances_render_pass(ctxt, fragment);

	for (int y = 0; y < fragment->height; y++) {
		for (int x = 0; x < fragment->width; x++) {
			fragment->buf[y * fragment->pitch + x] = ctxt->distances.buf[(y + fragment->y) * ctxt->distances.width + (fragment->x + x)].cell->color;
		}
	}
}


static void voronoi_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	voronoi_context_t	*ctxt = (voronoi_context_t *)context;

	ctxt->distances.recalc_needed = 0;
}


static int voronoi_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	voronoi_module = {
	.create_context = voronoi_create_context,
	.destroy_context = voronoi_destroy_context,
	.prepare_frame = voronoi_prepare_frame,
	.render_fragment = voronoi_render_fragment,
	.finish_frame = voronoi_finish_frame,
	.setup = voronoi_setup,
	.name = "voronoi",
	.description = "Voronoi diagram (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


static int voronoi_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*n_cells;
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
	til_setting_t	*randomize;
	const char	*bool_values[] = {
				"off",
				"on",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_setting(settings,
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

	r = til_settings_get_and_describe_setting(settings,
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

		setup = til_setup_new(settings, sizeof(*setup), NULL, &voronoi_module);
		if (!setup)
			return -ENOMEM;

		if (sscanf(n_cells->value, "%zu", &setup->n_cells) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, n_cells, res_setting, -EINVAL);

		if (!strcasecmp(randomize->value, "on"))
			setup->randomize = 1;

		*res_setup = &setup->til_setup;
	}
	return 0;
}
