#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_util.h"

/* Copyright (C) 2019 - Vito Caputo <vcaputo@pengaru.com> */

typedef struct montage_context_t {
	til_module_context_t	til_module_context;
	const til_module_t	**modules;
	til_module_context_t	**contexts;
	size_t			n_modules;
} montage_context_t;

static til_module_context_t * montage_create_context(til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup);
static void montage_destroy_context(til_module_context_t *context);
static void montage_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan);
static void montage_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr);


til_module_t	montage_module = {
	.create_context = montage_create_context,
	.destroy_context = montage_destroy_context,
	.prepare_frame = montage_prepare_frame,
	.render_fragment = montage_render_fragment,
	.name = "montage",
	.description = "Rototiller montage (threaded)",
};


static til_module_context_t * montage_create_context(til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	const til_module_t	**modules, *rtv_module, *compose_module;
	size_t			n_modules;
	montage_context_t	*ctxt;

	ctxt = til_module_context_new(stream, sizeof(montage_context_t), seed, ticks, n_cpus, path);
	if (!ctxt)
		return NULL;

	til_get_modules(&modules, &n_modules);

	ctxt->modules = calloc(n_modules, sizeof(til_module_t *));
	if (!ctxt->modules) {
		free(ctxt);

		return NULL;
	}

	rtv_module = til_lookup_module("rtv");
	compose_module = til_lookup_module("compose");

	for (size_t i = 0; i < n_modules; i++) {
		const til_module_t	*module = modules[i];

		if (module == &montage_module ||	/* prevents recursion */
		    module == rtv_module ||		/* also prevents recursion, rtv can run montage */
		    module == compose_module ||		/* also prevents recursion, compose can run montage */
		    (module->flags & (TIL_MODULE_HERMETIC | TIL_MODULE_EXPERIMENTAL))) /* prevents breakages */
			continue;

							/* XXX FIXME: there's another recursive problem WRT threaded
							 * rendering; even if rtv or compose don't run montage, they
							 * render threaded modules in a threaded fashion, while montage
							 * is already performing a threaded render, and the threaded
							 * rendering api isn't reentrant like that so things get hung
							 * when montage runs e.g. compose with a layer using a threaded
							 * module.  It's something to fix eventually, maybe just make
							 * the rototiler_module_render() function detect nested renders
							 * and turn nested threaded renders into synchronous renders.
							 * For now montage will just skip nested rendering modules.
							 */

		ctxt->modules[ctxt->n_modules++] = module;
	}

	ctxt->contexts = calloc(ctxt->n_modules, sizeof(til_module_context_t *));
	if (!ctxt->contexts) {
		free(ctxt->modules);
		free(ctxt);

		return NULL;
	}

	for (size_t i = 0; i < ctxt->n_modules; i++) {
		const til_module_t	*module = ctxt->modules[i];
		til_setup_t		*setup = NULL;

		(void) til_module_randomize_setup(module, rand_r(&seed), &setup, NULL);

		/* FIXME errors */
		(void) til_module_create_context(module, stream, rand_r(&seed), ticks, 1, path, setup, &ctxt->contexts[i]);

		til_setup_free(setup);
	}

	return &ctxt->til_module_context;
}


static void montage_destroy_context(til_module_context_t *context)
{
	montage_context_t	*ctxt = (montage_context_t *)context;

	for (int i = 0; i < ctxt->n_modules; i++)
		til_module_context_free(ctxt->contexts[i]);

	free(ctxt->contexts);
	free(ctxt->modules);
	free(ctxt);
}


/* this is a hacked up derivative of til_fb_fragment_tile_single() */
static int montage_fragment_tile(const til_fb_fragment_t *fragment, unsigned tile_width, unsigned tile_height, unsigned number, til_fb_fragment_t *res_fragment)
{
	unsigned	w = fragment->width / tile_width, h = fragment->height / tile_height;
	unsigned	x, y, xoff, yoff;

	assert(fragment);
	assert(res_fragment);

#if 0
	/* total coverage isn't important in montage, leave blank gaps */
	/* I'm keeping this here for posterity though and to record a TODO:
	 * it might be desirable to try center the montage when there must be gaps,
	 * rather than letting the gaps always fall on the far side.
	 */
	if (w * tile_width < fragment->width)
		w++;

	if (h * tile_height < fragment->height)
		h++;
#endif

	y = number / w;
	if (y >= h)
		return 0;

	x = number - (y * w);

	xoff = x * tile_width;
	yoff = y * tile_height;

	if (fragment->texture) {
		assert(res_fragment->texture);
		assert(fragment->frame_width == fragment->texture->frame_width);
		assert(fragment->frame_height == fragment->texture->frame_height);
		assert(fragment->width == fragment->texture->width);
		assert(fragment->height == fragment->texture->height);
		assert(fragment->x == fragment->texture->x);
		assert(fragment->y == fragment->texture->y);

		*(res_fragment->texture) = (til_fb_fragment_t){
					.buf = fragment->texture->buf + (yoff * fragment->texture->pitch) + xoff,
					.x = 0,								/* fragment is a new frame */
					.y = 0,								/* fragment is a new frame */
					.width = MIN(fragment->width - xoff, tile_width),
					.height = MIN(fragment->height - yoff, tile_height),
					.frame_width = MIN(fragment->width - xoff, tile_width),		/* fragment is a new frame */
					.frame_height = MIN(fragment->height - yoff, tile_height),	/* fragment is a new frame */
					.stride = fragment->texture->stride + (fragment->width - MIN(fragment->width - xoff, tile_width)),
					.pitch = fragment->texture->pitch,
					.cleared = fragment->texture->cleared,
				};
	}

	*res_fragment = (til_fb_fragment_t){
				.texture = fragment->texture ? res_fragment->texture : NULL,
				.buf = fragment->buf + (yoff * fragment->pitch) + xoff,
				.x = 0,								/* fragment is a new frame */
				.y = 0,								/* fragment is a new frame */
				.width = MIN(fragment->width - xoff, tile_width),
				.height = MIN(fragment->height - yoff, tile_height),
				.frame_width = MIN(fragment->width - xoff, tile_width),		/* fragment is a new frame */
				.frame_height = MIN(fragment->height - yoff, tile_height),	/* fragment is a new frame */
				.stride = fragment->stride + (fragment->width - MIN(fragment->width - xoff, tile_width)),
				.pitch = fragment->pitch,
				.number = number,
				.cleared = fragment->cleared,
			};

	return 1;
}


/* The fragmenter in montage is serving double-duty:
 * 1. it divides the frame into subfragments for threaded rendering
 * 2. it determines which modules will be rendered where via fragment->number
 */
static int montage_fragmenter(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	montage_context_t	*ctxt = (montage_context_t *)context;
	float			root = sqrtf(ctxt->n_modules);
	unsigned		tile_width = fragment->frame_width / ceilf(root);	/* screens are wide, always give excess to the width */
	unsigned		tile_height = fragment->frame_height / rintf(root);	/* only give to the height when fraction is >= .5f */
	int			ret;

	/* XXX: this could all be more clever and make some tiles bigger than others to deal with fractional square roots,
	 * but this is good enough for now considering the simplicity.
	 */
	ret = montage_fragment_tile(fragment, tile_width, tile_height, number, res_fragment);
	if (!ret)
		return 0;

	return ret;
}


static void montage_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	*res_frame_plan = (til_frame_plan_t){ .fragmenter = montage_fragmenter };
}


static void montage_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	montage_context_t	*ctxt = (montage_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	if (fragment->number >= ctxt->n_modules) {
		til_fb_fragment_clear(fragment);

		return;
	}

	til_module_render(ctxt->contexts[fragment->number], stream, ticks, fragment_ptr);
}
