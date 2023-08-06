#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_util.h"

/* Copyright (C) 2019 - Vito Caputo <vcaputo@pengaru.com> */

#define MONTAGE_DEFAULT_TILE_MODULES	"all"
#define MONTAGE_DEFAULT_TILE_MODULE	"blank"	/* not really sure what's best here, montage is sort of silly beyond diagnostic use */

typedef struct montage_context_t {
	til_module_context_t	til_module_context;

	til_module_context_t	*tile_contexts[];
} montage_context_t;

typedef struct montage_setup_tile_t {
	til_setup_t		*setup;
} montage_setup_tile_t;

typedef struct montage_setup_t {
	til_setup_t		til_setup;

	size_t			n_tiles;
	montage_setup_tile_t	tiles[];
} montage_setup_t;


static til_module_context_t * montage_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup);
static void montage_destroy_context(til_module_context_t *context);
static void montage_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan);
static void montage_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr);
static int montage_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	montage_module = {
	.create_context = montage_create_context,
	.destroy_context = montage_destroy_context,
	.prepare_frame = montage_prepare_frame,
	.render_fragment = montage_render_fragment,
	.setup = montage_setup,
	.name = "montage",
	.description = "Rototiller montage (threaded)",
};


static til_module_context_t * montage_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	montage_setup_t		*s = (montage_setup_t *)setup;
	montage_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(montage_context_t) + s->n_tiles * sizeof(ctxt->tile_contexts[0]), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	for (size_t i = 0; i < s->n_tiles; i++) {
		const til_module_t	*module = s->tiles[i].setup->creator;

		(void) til_module_create_context(module, stream, rand_r(&seed), ticks, 1, s->tiles[i].setup, &ctxt->tile_contexts[i]);
	}

	return &ctxt->til_module_context;
}


static void montage_destroy_context(til_module_context_t *context)
{
	montage_context_t	*ctxt = (montage_context_t *)context;

	for (int i = 0; i < ((montage_setup_t *)context->setup)->n_tiles; i++)
		til_module_context_free(ctxt->tile_contexts[i]);

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
	float		root = sqrtf(((montage_setup_t *)context->setup)->n_tiles);
	unsigned	tile_width = fragment->frame_width / ceilf(root);	/* screens are wide, always give excess to the width */
	unsigned	tile_height = fragment->frame_height / rintf(root);	/* only give to the height when fraction is >= .5f */
	int		ret;

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

	if (fragment->number >= ((montage_setup_t *)context->setup)->n_tiles) {
		til_fb_fragment_clear(fragment);

		return;
	}

	til_module_render(ctxt->tile_contexts[fragment->number], stream, ticks, fragment_ptr);
}


/* this implements the "all" -> "mod0name,mod1name,mod2name..." alias expansion */
static const char * montage_tiles_setting_override(const char *value)
{
	const char	*exclusions[] = {
				"montage",
				"compose",
				"rtv",
				NULL
			};

	if (strcasecmp(value, "all"))
		return value;

	return til_get_module_names((TIL_MODULE_HERMETIC|TIL_MODULE_EXPERIMENTAL|TIL_MODULE_BUILTIN), exclusions);
}


static void montage_setup_free(til_setup_t *setup)
{
	montage_setup_t	*s = (montage_setup_t *)setup;

	if (s) {
		for (size_t i = 0; i < s->n_tiles; i++)
			til_setup_free(s->tiles[i].setup);
		free(setup);
	}
}


static int montage_tile_module_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	return til_module_setup_full(settings,
				     res_setting,
				     res_desc,
				     res_setup,
				     "Tile module name",
				     MONTAGE_DEFAULT_TILE_MODULE,
				     (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC),
				     NULL);
}


static int montage_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const til_settings_t	*tiles_settings;
	const char		*tiles;
	int			r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Comma-separated list of modules, in left-to-right order, wraps top-down. (\"all\" for all)",
							.key = "tiles",
							.preferred = MONTAGE_DEFAULT_TILE_MODULES,
						// TODO	.random = montage_random_tiles_setting,
							.override = montage_tiles_setting_override,
							.as_nested_settings = 1,
						},
						&tiles, /* XXX: unused in raw-value form, we want the settings instance */
						res_setting,
						res_desc);
	if (r)
		return r;

	assert(res_setting && *res_setting && (*res_setting)->value_as_nested_settings);
	tiles_settings = (*res_setting)->value_as_nested_settings;
	{
		til_setting_t	*tile_setting;

		for (size_t i = 0; til_settings_get_value_by_idx(tiles_settings, i, &tile_setting); i++) {
			if (!tile_setting->value_as_nested_settings) {
				r = til_setting_desc_new(	tiles_settings,
								&(til_setting_spec_t){
									.as_nested_settings = 1,
								}, res_desc);
				if (r < 0)
					return r;

				*res_setting = tile_setting;

				return 1;
			}
		}

		for (size_t i = 0; til_settings_get_value_by_idx(tiles_settings, i, &tile_setting); i++) {
			r = montage_tile_module_setup(tile_setting->value_as_nested_settings,
						      res_setting,
						      res_desc,
						      NULL); /* XXX: note no res_setup, must defer finalize */
			if (r)
				return r;
		}
	}

	if (res_setup) {
		size_t			n_tiles = til_settings_get_count(tiles_settings);
		til_setting_t		*tile_setting;
		montage_setup_t		*setup;

		setup = til_setup_new(settings, sizeof(*setup) + n_tiles * sizeof(*setup->tiles), montage_setup_free, &montage_module);
		if (!setup)
			return -ENOMEM;

		setup->n_tiles = n_tiles;

		for (size_t i = 0; til_settings_get_value_by_idx(tiles_settings, i, &tile_setting); i++) {
			r = montage_tile_module_setup(tile_setting->value_as_nested_settings,
						      res_setting,
						      res_desc,
						      &setup->tiles[i].setup); /* finalize! */
			if (r < 0) {
				til_setup_free(&setup->til_setup);
				return r;
			}

			assert(r == 0);
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}
