/*
 *  Copyright (C) 2024-2025 - Vito Caputo - <vcaputo@pengaru.com>
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

#include <stdint.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

/* This implements a "droste effect", also known as an infinity mirror:
 * https://en.wikipedia.org/wiki/Droste_effect
 * https://en.wikipedia.org/wiki/Infinity_mirror
 */

/* Some potential TODO items:
 *
 * - Fractional, or at least runtime-configurable scaling... though
 *   exposing a tap for it would be fun
 *
 * - Runtime-configurable multisampled scaling (slow though)
 *
 * - The current implementation is very simple but relies on the
 *   preservation of original contents in til_fb_fragment_snapshot(),
 *   which causes a full copy.  This is wasteful since we only want
 *   the unzoomed periphery preserved from the original.  Maybe there
 *   should be a clip mask for the preservation in
 *   til_fb_fragment_snapshot(), or just do the peripheral copy ourselves
 *   and stop asking the snapshot code to do that for us.
 *
 * - The base module is currently always setup but conditionally used if
 *   (!fragment->cleared).  It should probably be possible to specify
 *   forcing the base module's render
 *
 */

#define DROSTE_DEFAULT_BASE_MODULE	"blinds"

typedef struct droste_context_t {
	til_module_context_t	til_module_context;

	til_module_context_t	*base_module_context;	/* a base module is used for non-overlay situations */
	til_fb_fragment_t	*snapshot;
} droste_context_t;

typedef struct droste_setup_t {
	til_setup_t		til_setup;

	til_setup_t		*base_module_setup;
} droste_setup_t;


static til_module_context_t * droste_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	droste_context_t	*ctxt;

	if (!((droste_setup_t *)setup)->base_module_setup)
		return NULL;

	ctxt = til_module_context_new(module, sizeof(droste_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	{
		const til_module_t	*module = ((droste_setup_t *)setup)->base_module_setup->creator;

		if (til_module_create_contexts(module,
					       stream,
					       seed,
					       ticks,
					       n_cpus,
					       ((droste_setup_t *)setup)->base_module_setup,
					       1,
					       &ctxt->base_module_context) < 0)
			return til_module_context_free(&ctxt->til_module_context);
	}

	return &ctxt->til_module_context;
}


static void droste_destroy_context(til_module_context_t *context)
{
	droste_context_t	*ctxt = (droste_context_t *)context;

	if (ctxt->snapshot)
		ctxt->snapshot = til_fb_fragment_reclaim(ctxt->snapshot);

	til_module_context_free(ctxt->base_module_context);
	free(context);
}


/* derived from til_fragmenter_slice_per_cpu_x16(), but tweaked to only fragment the inset area */
static int droste_fragmenter(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	til_fb_fragment_t	inset = *fragment;
	unsigned		slice, yoff;

	assert(fragment);
	assert(res_fragment);

	inset.width = fragment->width >> 1;
	inset.height = fragment->height >> 1;
	inset.frame_width = inset.width;
	inset.frame_height = inset.height;
	inset.x = inset.y = 0;
	inset.buf += inset.pitch * ((fragment->height - inset.height) >> 1) + ((fragment->width - inset.width) >> 1);
	inset.stride += fragment->width >> 1;

	slice = MAX(inset.height / context->n_cpus * 16, 1);
	yoff = slice * number;

	if (yoff >= inset.height)
		return 0;

	if (fragment->texture) {
		til_fb_fragment_t	inset_texture;

		inset_texture = *(fragment->texture);
		inset.texture = &inset_texture;

		/* TODO */

		assert(res_fragment->texture);
		assert(fragment->frame_width == fragment->texture->frame_width);
		assert(fragment->frame_height == fragment->texture->frame_height);
		assert(fragment->width == fragment->texture->width);
		assert(fragment->height == fragment->texture->height);
		assert(fragment->x == fragment->texture->x);
		assert(fragment->y == fragment->texture->y);

		*(res_fragment->texture) = (til_fb_fragment_t){
				.buf = fragment->texture->buf + yoff * fragment->texture->pitch,
				.x = fragment->x,
				.y = fragment->y + yoff,
				.width = fragment->width,
				.height = MIN(fragment->height - yoff, slice),
				.frame_width = fragment->frame_width,
				.frame_height = fragment->frame_height,
				.stride = fragment->texture->stride,
				.pitch = fragment->texture->pitch,
				.cleared = fragment->texture->cleared,
		};

	}

	*res_fragment = (til_fb_fragment_t){
				.texture = inset.texture ? res_fragment->texture : NULL,
				.buf = inset.buf + yoff * inset.pitch,
				.x = inset.x,
				.y = inset.y + yoff,
				.width = inset.width,
				.height = MIN(inset.height - yoff, slice),
				.frame_width = inset.frame_width,
				.frame_height = inset.frame_height,
				.stride = inset.stride,
				.pitch = inset.pitch,
				.number = number,
				.cleared = inset.cleared,
			};

	return 1;

}


/* Prepare a frame for concurrent drawing of fragment using multiple fragments */
static void droste_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	droste_context_t	*ctxt = (droste_context_t *)context;

	if (!(*fragment_ptr)->cleared)
		til_module_render(ctxt->base_module_context, stream, ticks, fragment_ptr);

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = droste_fragmenter };

	{
		til_fb_fragment_t	*fragment = *fragment_ptr;
		til_fb_fragment_t	*snapshot = ctxt->snapshot;

		if (!snapshot)
			return;

		if (fragment->frame_width != snapshot->frame_width ||
		    fragment->frame_height != snapshot->frame_height ||
		    fragment->height != snapshot->height ||
		    fragment->width != snapshot->width) {

			/* discard the snapshot which will prevent doing anything this frame,
			 * since it doesn't match the incoming fragment (like a resize situation)
			 */
			ctxt->snapshot = til_fb_fragment_reclaim(ctxt->snapshot);

			return;
		}

		/* TODO: if we're not used as an overlay, here'd be a good place to generate something or
		 * just use another module as a base layer...  until we do something sensible here, we should
		 * keep this as an experimental module so it doesn't get used by automation as a base layer..
		 * it also needs something to show in montage.
		 */
	}
}


static void droste_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	droste_context_t	*ctxt = (droste_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;
	til_fb_fragment_t	*snapshot = ctxt->snapshot;

	if (!snapshot)
		return;

	for (unsigned y = fragment->y; y < fragment->y + fragment->height; y++) {
		for (unsigned x = fragment->x; x < fragment->x + fragment->width; x++) {
			uint32_t	color;

			color = til_fb_fragment_get_pixel_clipped(snapshot, x << 1, y << 1);
			til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, color);
		}
	}
}


static int droste_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned int ticks, til_fb_fragment_t **fragment_ptr)
{
	droste_context_t	*ctxt = (droste_context_t *)context;

	/* if we have a stowed frame clean it up */
	if (ctxt->snapshot)
		ctxt->snapshot = til_fb_fragment_reclaim(ctxt->snapshot);

	/* stow the new frame for the next time around to sample from */
	ctxt->snapshot = til_fb_fragment_snapshot(fragment_ptr, 1);

	return 0;
}


static void droste_setup_free(til_setup_t *setup)
{
	droste_setup_t	*s = (droste_setup_t *)setup;

	til_setup_free(s->base_module_setup);
	free(setup);
}


static int droste_base_module_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	return til_module_setup_full(settings,
				     res_setting,
				     res_desc,
				     res_setup,
				     "Base module name",
				     DROSTE_DEFAULT_BASE_MODULE,
				     (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC | TIL_MODULE_AUDIO_ONLY),
				     NULL);
}


static int droste_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	droste_module = {
	.create_context = droste_create_context,
	.destroy_context = droste_destroy_context,
	.prepare_frame = droste_prepare_frame,
	.render_fragment = droste_render_fragment,
	.finish_frame = droste_finish_frame,
	.setup = droste_setup,
	.name = "droste",
	.description = "Droste effect (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


static int droste_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t		*base_module;
	const til_settings_t	*base_module_settings;
	const char		*base_module_values[] = {
					"blinds",
					"book",
					"moire",
					"plasma",
					"plato",
					"roto",
					NULL
				};
	int			r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Base module",
							.key = "base_module",
							.preferred = base_module_values[0],
							.values = base_module_values,
							.annotations = NULL,
							.as_nested_settings = 1,
						},
						&base_module, /* XXX: this isn't really of direct use now that it's a potentially full-blown settings string, see base_module_settings */
						res_setting,
						res_desc);
	if (r)
		return r;

	base_module_settings = base_module->value_as_nested_settings;
	assert(base_module_settings);

	r = droste_base_module_setup(base_module_settings,
				   res_setting,
				   res_desc,
				   NULL); /* XXX: note no res_setup, must defer finalize */
	if (r)
		return r;

	if (res_setup) {
		droste_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), droste_setup_free, &droste_module);
		if (!setup)
			return -ENOMEM;

		r = droste_base_module_setup(base_module_settings,
					   res_setting,
					   res_desc,
					   &setup->base_module_setup); /* finalize! */
		if (r < 0)
			return til_setup_free_with_ret_err(&setup->til_setup, r);

		assert(r == 0);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
