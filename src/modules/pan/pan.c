#include <math.h>

#include "til.h"
#include "til_fb.h"

/* Copyright (C) 2023 - Vito Caputo <vcaputo@pengaru.com> */

/* This implements a rudimentary panning module primarily for overlay use,
 *
 * TODO:
 * - the minimal default tile could be more clever
 * - runtime setting for panning velocity
 * - optimization
 * (see comments)
 */

#define PAN_DEFAULT_Y_COMPONENT	-.5
#define PAN_DEFAULT_X_COMPONENT	.25
#define PAN_DEFAULT_TILE_SIZE	32

typedef struct pan_context_t {
	til_module_context_t	til_module_context;

	til_fb_fragment_t	*snapshot;
	float			xoffset, yoffset;
	til_fb_fragment_t	tile;
	uint32_t		tile_buf[PAN_DEFAULT_TILE_SIZE * PAN_DEFAULT_TILE_SIZE];
} pan_context_t;

typedef struct pan_setup_t {
	til_setup_t		til_setup;

	float			x, y;
} pan_setup_t;


static til_module_context_t * pan_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	pan_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(pan_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->tile = (til_fb_fragment_t){
				.buf = ctxt->tile_buf,
				.frame_width = PAN_DEFAULT_TILE_SIZE,
				.frame_height = PAN_DEFAULT_TILE_SIZE,
				.width = PAN_DEFAULT_TILE_SIZE,
				.height = PAN_DEFAULT_TILE_SIZE,
				.pitch = PAN_DEFAULT_TILE_SIZE,
		     };

	{
		uint32_t	color = (((uint32_t)rand_r(&seed) & 0xff) << 16) |
					(((uint32_t)rand_r(&seed) & 0xff) << 8) |
					((uint32_t)rand_r(&seed) & 0xff);

		/* FIXME TODO: make a more interesting default pattern, still influenced by seed tho */
		for (int y = 0; y < PAN_DEFAULT_TILE_SIZE; y++) {
			for (int x = 0; x < PAN_DEFAULT_TILE_SIZE; x++) {
				uint32_t	c = (((x*y & 0xff) << 16) | ((x*y & 0xff) << 8) | (x*y & 0xff));

				ctxt->tile_buf[y * PAN_DEFAULT_TILE_SIZE + x] = color ^ c;
			}
		}
	}

	return &ctxt->til_module_context;
}


static void pan_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	pan_context_t		*ctxt = (pan_context_t *)context;
	pan_setup_t		*s = (pan_setup_t *)context->setup;
	til_fb_fragment_t	*fragment = *fragment_ptr;
	float			dt = (ticks - context->last_ticks) * .1f;
	til_fb_fragment_t	*snapshot = &ctxt->tile;

	if (fragment->cleared)
		ctxt->snapshot = til_fb_fragment_snapshot(fragment_ptr, 0);

	if (ctxt->snapshot)
		snapshot = ctxt->snapshot;

	ctxt->xoffset += dt * s->x;
	ctxt->xoffset = fmodf(ctxt->xoffset, snapshot->frame_width);
	ctxt->yoffset += dt * s->y;
	ctxt->yoffset = fmodf(ctxt->yoffset, snapshot->frame_height);

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu_x16 };

}


/* like til_fb_fragment_get_pixel_clipped, but wraps around (maybe move to til_fb.h?) */
static inline uint32_t pan_get_pixel_wrapped(til_fb_fragment_t *fragment, int x, int y)
{
	int	xcoord, ycoord;

	/* The need for such casts makes me wonder if til_fragment_t.*{width,height} should just be ints,
	 * which annoys me because those members should never actually be negative.  But without the casts,
	 * the modulos do the entirely wrong thing for negative coordinates.
	 */
	xcoord = x % (int)fragment->frame_width;
	if (xcoord < 0)
		xcoord += fragment->frame_width;

	ycoord = y % (int)fragment->frame_height;
	if (ycoord < 0)
		ycoord += fragment->frame_height;

	return til_fb_fragment_get_pixel_unchecked(fragment, xcoord, ycoord);
}


static void pan_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	pan_context_t		*ctxt = (pan_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;
	int			xoff, yoff;
	til_fb_fragment_t	*snapshot = ctxt->snapshot ? : &ctxt->tile;

	xoff = ctxt->xoffset;
	yoff = ctxt->yoffset;

	for (int y = 0; y < fragment->height; y++) {
		int	ycoord = fragment->y + y + yoff;

		for (int x = 0; x < fragment->width; x++) {
			int		xcoord = fragment->x + x + xoff;
			uint32_t	pixel;

			/* TODO: optimize this to at least do contiguous row copies,
			 * as-is it's doing modulos per-pixel!
			 */

			pixel = pan_get_pixel_wrapped(snapshot, xcoord, ycoord);
			til_fb_fragment_put_pixel_unchecked(fragment, 0, fragment->x + x, fragment->y + y, pixel);
		}
	}

	*fragment_ptr = fragment;
}


static int pan_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned int ticks, til_fb_fragment_t **fragment_ptr)
{
	pan_context_t	*ctxt = (pan_context_t *)context;

	if (ctxt->snapshot)
		ctxt->snapshot = til_fb_fragment_reclaim(ctxt->snapshot);

	return 0;
}


static int pan_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	pan_module = {
	.create_context = pan_create_context,
	.prepare_frame = pan_prepare_frame,
	.render_fragment = pan_render_fragment,
	.finish_frame = pan_finish_frame,
	.setup = pan_setup,
	.name = "pan",
	.description = "Simple panning effect (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


static int pan_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*x, *y;
	const char	*component_values[] = {
				"-1",
				"-.8",
				"-.7",
				"-.5",
				"-.25",
				"-.2",
				"-.1",
				"-.05",
				"0",
				".05",
				".1",
				".2",
				".25",
				".5",
				".7",
				".8",
				"1",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Pan direction vector X component",
							.key = "x",
							.preferred = TIL_SETTINGS_STR(PAN_DEFAULT_X_COMPONENT),
							.values = component_values,
							.annotations = NULL
						},
						&x,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Pan direction vector Y component",
							.key = "y",
							.preferred = TIL_SETTINGS_STR(PAN_DEFAULT_Y_COMPONENT),
							.values = component_values,
							.annotations = NULL
						},
						&y,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		pan_setup_t	*setup;
		float		l;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &pan_module);
		if (!setup)
			return -ENOMEM;

		if (sscanf(x->value, "%f", &setup->x) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, x, res_setting, -EINVAL);

		if (sscanf(y->value, "%f", &setup->y) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, y, res_setting, -EINVAL);

		l = sqrtf(setup->x * setup->x + setup->y * setup->y);
		if (l) {
			setup->x /= l;
			setup->y /= l;
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}
