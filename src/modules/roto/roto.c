#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

/* Some defines for the fixed-point stuff in render(). */
#define FIXED_TRIG_LUT_SIZE	4096			/* size of the cos/sin look-up tables */
#define FIXED_BITS		11			/* fractional bits */
#define FIXED_EXP		(1 << FIXED_BITS)	/* 2^FIXED_BITS */
#define FIXED_MASK		(FIXED_EXP - 1)		/* fractional part mask */
#define FIXED_COS(_rad)		roto_costab[(_rad) % FIXED_TRIG_LUT_SIZE]
#define FIXED_SIN(_rad)		roto_sintab[(_rad) % FIXED_TRIG_LUT_SIZE]
#define FIXED_MULT(_a, _b)	(((_a) * (_b)) >> FIXED_BITS)
#define FIXED_NEW(_i)		((_i) << FIXED_BITS)
#define FIXED_TO_INT(_f)	((_f) >> FIXED_BITS)

#define ROTO_TEXTURE_SIZE	256

typedef struct color_t {
	int	r, g, b;
} color_t;

typedef struct roto_context_t {
	til_module_context_t	til_module_context;
	unsigned		r, rr;
	color_t			palette[2];
	til_module_context_t	*fill_module_context;
	til_fb_fragment_t	fill_fb;
} roto_context_t;

typedef struct roto_setup_t {
	til_setup_t		til_setup;

	const til_module_t	*fill_module;
	til_setup_t		*fill_module_setup;
} roto_setup_t;

static int32_t	roto_costab[FIXED_TRIG_LUT_SIZE], roto_sintab[FIXED_TRIG_LUT_SIZE];
static uint8_t	roto_texture[ROTO_TEXTURE_SIZE][ROTO_TEXTURE_SIZE];


static void init_roto(uint8_t texture[ROTO_TEXTURE_SIZE][ROTO_TEXTURE_SIZE], int32_t *roto_costab, int32_t *roto_sintab)
{
	int	x, y, i;

	/* Generate simple checker pattern texture, nothing clever, feel free to play! */
	/* If you modify texture on every frame instead of only @ initialization you can
	 * produce some neat output.  These values are indexed into palette[] below. */
	for (y = 0; y < ROTO_TEXTURE_SIZE >> 1; y++) {
		for (x = 0; x < ROTO_TEXTURE_SIZE >> 1; x++)
			texture[y][x] = 1;
		for (; x < ROTO_TEXTURE_SIZE; x++)
			texture[y][x] = 0;
	}
	for (; y < ROTO_TEXTURE_SIZE; y++) {
		for (x = 0; x < ROTO_TEXTURE_SIZE >> 1; x++)
			texture[y][x] = 0;
		for (; x < ROTO_TEXTURE_SIZE; x++)
			texture[y][x] = 1;
	}

	/* Generate fixed-point cos & sin LUTs. */
	for (i = 0; i < FIXED_TRIG_LUT_SIZE; i++) {
		roto_costab[i] = ((cos((double)2*M_PI*i/FIXED_TRIG_LUT_SIZE))*FIXED_EXP);
		roto_sintab[i] = ((sin((double)2*M_PI*i/FIXED_TRIG_LUT_SIZE))*FIXED_EXP);
	}
}


static til_module_context_t * roto_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	static int	initialized;
	roto_context_t	*ctxt;

	if (!initialized) {
		initialized = 1;

		init_roto(roto_texture, roto_costab, roto_sintab);
	}

	ctxt = til_module_context_new(module, sizeof(roto_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	if (((roto_setup_t *)setup)->fill_module) {
		const til_module_t	*module = ((roto_setup_t *)setup)->fill_module;

		if (til_module_create_contexts(module,
					       stream,
					       seed,
					       ticks,
					       n_cpus,
					       ((roto_setup_t *)setup)->fill_module_setup,
					       1,
					       &ctxt->fill_module_context) < 0)
			return til_module_context_free(&ctxt->til_module_context);

		ctxt->fill_fb =	(til_fb_fragment_t){
					.buf = malloc(ROTO_TEXTURE_SIZE * ROTO_TEXTURE_SIZE * sizeof(uint32_t)),

					.frame_width = ROTO_TEXTURE_SIZE,
					.frame_height = ROTO_TEXTURE_SIZE,
					.width = ROTO_TEXTURE_SIZE,
					.height = ROTO_TEXTURE_SIZE,
					.pitch = ROTO_TEXTURE_SIZE,
				};
		if (!ctxt->fill_fb.buf)
			return til_module_context_free(&ctxt->til_module_context);
	}

	ctxt->r = rand_r(&seed);
	ctxt->rr = rand_r(&seed);

	return &ctxt->til_module_context;
}


static void roto_destroy_context(til_module_context_t *context)
{
	roto_context_t	*ctxt = (roto_context_t *)context;

	free(ctxt->fill_fb.buf);
	til_module_context_free(ctxt->fill_module_context);
	free(ctxt);
}


/* linearly interpolate between two colors, alpha is fixed-point value 0-FIXED_EXP. */
static inline color_t lerp_color(color_t *a, color_t *b, int alpha)
{
	/* TODO: This could be done without multiplies with a bit of effort,
	 * maybe a simple table mapping integer color deltas to shift values
	 * for shifting alpha which then gets simply added?  A table may not even
	 * be necessary, use the order of the delta to derive how much to shift
	 * alpha?
	 */
	color_t	c = {
			.r = a->r + FIXED_MULT(alpha, b->r - a->r),
			.g = a->g + FIXED_MULT(alpha, b->g - a->g),
			.b = a->b + FIXED_MULT(alpha, b->b - a->b),
		};

	return c;
}


/* Return the bilinearly interpolated color palette[texture[ty][tx]] (Anti-Aliasing) */
/* tx, ty are fixed-point for fractions, palette colors are also in fixed-point format. */
static uint32_t bilerp_color(uint8_t texture[ROTO_TEXTURE_SIZE][ROTO_TEXTURE_SIZE], color_t *palette, int tx, int ty)
{
	uint8_t	itx = FIXED_TO_INT(tx), ity = FIXED_TO_INT(ty);
	color_t	n_color, s_color, color;
	int	x_alpha, y_alpha;
	uint8_t	nw, ne, sw, se;

	/* We need the 4 texels constituting a 2x2 square pattern to interpolate.
	 * A point tx,ty can only intersect one texel; one corner of the 2x2 square.
	 * Where relative to the corner's center the intersection occurs determines which corner has been intersected,
	 * and the other corner texels may then be addressed relative to that corner.
	 * Alpha values must also be determined for both axis, these values describe the position between
	 * the 2x2 texel centers the intersection occurred, aka the weight or bias.
	 * Once the two alpha values are known, linear interpolation between the texel colors is trivial.
	 */

	if ((ty & FIXED_MASK) > (FIXED_EXP >> 1)) {
		y_alpha = ty & (FIXED_MASK >> 1);

		if ((tx & (FIXED_MASK)) > (FIXED_EXP >> 1)) {
			nw = texture[ity][itx];
			ne = texture[ity][(uint8_t)(itx + 1)];
			sw = texture[(uint8_t)(ity + 1)][itx];
			se = texture[(uint8_t)(ity + 1)][(uint8_t)(itx + 1)];

			x_alpha = tx & (FIXED_MASK >> 1);
		} else {
			ne = texture[ity][itx];
			nw = texture[ity][(uint8_t)(itx - 1)];
			se = texture[(uint8_t)(ity + 1)][itx];
			sw = texture[(uint8_t)(ity + 1)][(uint8_t)(itx - 1)];

			x_alpha = (FIXED_EXP >> 1) + (tx & (FIXED_MASK >> 1));
		}
	} else {
		y_alpha = (FIXED_EXP >> 1) + (ty & (FIXED_MASK >> 1));

		if ((tx & (FIXED_MASK)) > (FIXED_EXP >> 1)) {
			sw = texture[ity][itx];
			se = texture[ity][(uint8_t)(itx + 1)];
			nw = texture[(uint8_t)(ity - 1)][itx];
			ne = texture[(uint8_t)(ity - 1)][(uint8_t)(itx + 1)];

			x_alpha = tx & (FIXED_MASK >> 1);
		} else {
			se = texture[ity][itx];
			sw = texture[ity][(uint8_t)(itx - 1)];
			ne = texture[(uint8_t)(ity - 1)][itx];
			nw = texture[(uint8_t)(ity - 1)][(uint8_t)(itx - 1)];

			x_alpha = (FIXED_EXP >> 1) + (tx & (FIXED_MASK >> 1));
		}
	}

	/* Skip interpolation of same colors, a substantial optimization with plain textures like the checker pattern */
	if (nw == ne) {
		if (ne == sw && sw == se)
			return (FIXED_TO_INT(palette[sw].r) << 16) | (FIXED_TO_INT(palette[sw].g) << 8) | FIXED_TO_INT(palette[sw].b);

		n_color = palette[nw];
	} else {
		n_color = lerp_color(&palette[nw], &palette[ne], x_alpha);
	}

	if (sw == se) {
		s_color = palette[sw];
	} else {
		s_color = lerp_color(&palette[sw], &palette[se], x_alpha);
	}

	color = lerp_color(&n_color, &s_color, y_alpha);

	return (FIXED_TO_INT(color.r) << 16) | (FIXED_TO_INT(color.g) << 8) | FIXED_TO_INT(color.b);
}


static inline color_t * pixel32_to_color(uint32_t pixel, color_t *res_color)
{
	*res_color = (color_t){
		.r = FIXED_NEW((pixel >> 16) & 0xff),
		.g = FIXED_NEW((pixel >> 8) & 0xff),
		.b = FIXED_NEW((pixel) & 0xff),
	};

	return res_color;
}


/* Return the bilinearly interpolated color palette[texture[ty][tx]] (Anti-Aliasing) */
/* tx, ty are fixed-point for fractions, palette colors are also in fixed-point format. */
static uint32_t bilerp_color_pixel32(uint32_t texture[ROTO_TEXTURE_SIZE][ROTO_TEXTURE_SIZE], int tx, int ty)
{
	uint8_t		itx = FIXED_TO_INT(tx), ity = FIXED_TO_INT(ty);
	color_t		n_color, s_color, color;
	int		x_alpha, y_alpha;
	uint32_t	nw, ne, sw, se;

	/* We need the 4 texels constituting a 2x2 square pattern to interpolate.
	 * A point tx,ty can only intersect one texel; one corner of the 2x2 square.
	 * Where relative to the corner's center the intersection occurs determines which corner has been intersected,
	 * and the other corner texels may then be addressed relative to that corner.
	 * Alpha values must also be determined for both axis, these values describe the position between
	 * the 2x2 texel centers the intersection occurred, aka the weight or bias.
	 * Once the two alpha values are known, linear interpolation between the texel colors is trivial.
	 */

	if ((ty & FIXED_MASK) > (FIXED_EXP >> 1)) {
		y_alpha = ty & (FIXED_MASK >> 1);

		if ((tx & (FIXED_MASK)) > (FIXED_EXP >> 1)) {
			nw = texture[ity][itx];
			ne = texture[ity][(uint8_t)(itx + 1)];
			sw = texture[(uint8_t)(ity + 1)][itx];
			se = texture[(uint8_t)(ity + 1)][(uint8_t)(itx + 1)];

			x_alpha = tx & (FIXED_MASK >> 1);
		} else {
			ne = texture[ity][itx];
			nw = texture[ity][(uint8_t)(itx - 1)];
			se = texture[(uint8_t)(ity + 1)][itx];
			sw = texture[(uint8_t)(ity + 1)][(uint8_t)(itx - 1)];

			x_alpha = (FIXED_EXP >> 1) + (tx & (FIXED_MASK >> 1));
		}
	} else {
		y_alpha = (FIXED_EXP >> 1) + (ty & (FIXED_MASK >> 1));

		if ((tx & (FIXED_MASK)) > (FIXED_EXP >> 1)) {
			sw = texture[ity][itx];
			se = texture[ity][(uint8_t)(itx + 1)];
			nw = texture[(uint8_t)(ity - 1)][itx];
			ne = texture[(uint8_t)(ity - 1)][(uint8_t)(itx + 1)];

			x_alpha = tx & (FIXED_MASK >> 1);
		} else {
			se = texture[ity][itx];
			sw = texture[ity][(uint8_t)(itx - 1)];
			ne = texture[(uint8_t)(ity - 1)][itx];
			nw = texture[(uint8_t)(ity - 1)][(uint8_t)(itx - 1)];

			x_alpha = (FIXED_EXP >> 1) + (tx & (FIXED_MASK >> 1));
		}
	}

	/* Skip interpolation of same colors, a substantial optimization with plain textures like the checker pattern */
	if (nw == ne) {
		if (ne == sw && sw == se)
			return sw;

		pixel32_to_color(nw, &n_color);
	} else {
		n_color = lerp_color(pixel32_to_color(nw, &(color_t){}),
				     pixel32_to_color(ne, &(color_t){}),
				     x_alpha);
	}

	if (sw == se) {
		pixel32_to_color(sw, &s_color);
	} else {
		s_color = lerp_color(pixel32_to_color(sw, &(color_t){}),
				     pixel32_to_color(se, &(color_t){}),
				     x_alpha);
	}

	color = lerp_color(&n_color, &s_color, y_alpha);

	return (FIXED_TO_INT(color.r) << 16) | (FIXED_TO_INT(color.g) << 8) | FIXED_TO_INT(color.b);
}


/* prepare a frame for concurrent rendering */
static void roto_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	roto_context_t	*ctxt = (roto_context_t *)context;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu };

	// This governs the rotation and color cycle.
	if (ticks != context->last_ticks) {
		ctxt->r += FIXED_TO_INT(FIXED_MULT(FIXED_SIN(ctxt->rr), FIXED_NEW(16)));
		ctxt->rr += (ticks - context->last_ticks) >> 2;

		/* Vary the colors, this is just a mashup of sinusoidal rgb values. */
		ctxt->palette[0].r = (FIXED_MULT(FIXED_COS(ctxt->rr), FIXED_NEW(127)) + FIXED_NEW(128));
		ctxt->palette[0].g = (FIXED_MULT(FIXED_SIN(ctxt->rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
		ctxt->palette[0].b = (FIXED_MULT(FIXED_COS(ctxt->rr / 3), FIXED_NEW(127)) + FIXED_NEW(128));

		ctxt->palette[1].r = (FIXED_MULT(FIXED_SIN(ctxt->rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
		ctxt->palette[1].g = (FIXED_MULT(FIXED_COS(ctxt->rr / 2), FIXED_NEW(127)) + FIXED_NEW(128));
		ctxt->palette[1].b = (FIXED_MULT(FIXED_SIN(ctxt->rr), FIXED_NEW(127)) + FIXED_NEW(128));
	}

	if (ctxt->fill_module_context) {
		til_fb_fragment_t	*fb_ptr = &ctxt->fill_fb;

		ctxt->fill_fb.cleared = 0;
		til_module_render(ctxt->fill_module_context, stream, ticks, &fb_ptr);
	}
}


/* Draw a rotating checkered 256x256 texture into fragment. */
static void roto_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	roto_context_t		*ctxt = (roto_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	int		frame_width = fragment->frame_width, frame_height = fragment->frame_height;
	int		y_cos_r, y_sin_r, x_cos_r, x_sin_r, x_cos_r_init, x_sin_r_init, cos_r, sin_r;
	uint32_t	*buf = fragment->buf;

	/* This is all done using fixed-point in the hopes of being faster, and yes assumptions
	 * are being made WRT the overflow of tx/ty as well, only tested on x86_64. */
	cos_r = FIXED_COS(ctxt->r);
	sin_r = FIXED_SIN(ctxt->r);

	/* The dimensions are cut in half and negated to center the rotation. */
	/* The [xy]_{sin,cos}_r variables are accumulators to replace multiplication with addition. */
	x_cos_r_init = FIXED_MULT(-FIXED_NEW(frame_width / 2) + FIXED_NEW(fragment->x), cos_r);
	x_sin_r_init = FIXED_MULT(-FIXED_NEW(frame_width / 2) + FIXED_NEW(fragment->x), sin_r);

	y_cos_r = FIXED_MULT(-FIXED_NEW(frame_height / 2) + FIXED_NEW(fragment->y), cos_r);
	y_sin_r = FIXED_MULT(-FIXED_NEW(frame_height / 2) + FIXED_NEW(fragment->y), sin_r);

	for (int y = 0; y < fragment->height; y++) {

		x_cos_r = x_cos_r_init;
		x_sin_r = x_sin_r_init;

		if (ctxt->fill_module_context) {
			for (int x = 0; x < fragment->width; x++, buf++) {
				/* TODO: it would be interesting to support an overlay mode where we alpha blend this into the existing surface,
				 * so fill_modules that were overlayable would overlay in the rotated+tiled form...
				 */
				*buf = bilerp_color_pixel32((uint32_t (*)[ROTO_TEXTURE_SIZE])ctxt->fill_fb.buf, x_sin_r - y_cos_r, y_sin_r + x_cos_r);

				x_cos_r += cos_r;
				x_sin_r += sin_r;
			}

		} else {
			for (int x = 0; x < fragment->width; x++, buf++) {
				*buf = bilerp_color(roto_texture, ctxt->palette, x_sin_r - y_cos_r, y_sin_r + x_cos_r);

				x_cos_r += cos_r;
				x_sin_r += sin_r;
			}
		}

		buf += fragment->stride;
		y_cos_r += cos_r;
		y_sin_r += sin_r;
	}
}


static void roto_setup_free(til_setup_t *setup)
{
	roto_setup_t	*s = (roto_setup_t *)setup;

	if (s) {
		til_setup_free(s->fill_module_setup);
		free(setup);
	}
}


static int roto_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char		*fill_module, *fill_module_name;
	const til_settings_t	*fill_module_settings;
	til_setting_t		*fill_module_setting;
	const char		*fill_module_values[] = {
					"none",
					"blinds",
					"checkers",
					"moire",
					"pixbounce",
					"plato",
					"roto",
					"shapes",
					"spiro",
					"stars",
					NULL
				};
	int			r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Filled module (\"none\" for classic roto)",
							.key = "fill_module",
							.preferred = fill_module_values[0],
							.values = fill_module_values,
							.annotations = NULL,
							.as_nested_settings = 1,
						},
						&fill_module, /* XXX: this isn't really of direct use now that it's a potentially full-blown settings string, see fill_module_settings */
						res_setting,
						res_desc);
	if (r)
		return r;

	assert(res_setting && *res_setting);
	assert((*res_setting)->value_as_nested_settings);

	fill_module_settings = (*res_setting)->value_as_nested_settings;
	fill_module_name = til_settings_get_value_by_idx(fill_module_settings, 0, &fill_module_setting);

	if (!fill_module_name || !fill_module_setting->desc) {
		r = til_setting_desc_new(fill_module_settings,
					&(til_setting_spec_t){
						.name = "Fill module name",
						.preferred = "none",
						.as_label = 1,
					},
					res_desc);
		if (r < 0)
			return r;

		*res_setting = fill_module_name ? fill_module_setting : NULL;

		return 1;
	}

	if (strcasecmp(fill_module_name, "none")) {
		const til_module_t	*mod = til_lookup_module(fill_module_name);

		if (!mod) {
			*res_setting = fill_module_setting;

			return -EINVAL;
		}

		if (mod->setup) {
			r = mod->setup(fill_module_settings, res_setting, res_desc, NULL);
			if (r)
				return r;
		}
	}



	if (res_setup) {
		roto_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), roto_setup_free);
		if (!setup)
			return -ENOMEM;

		if (strcasecmp(fill_module_name, "none")) {
			setup->fill_module = til_lookup_module(fill_module_name);
			if (!setup->fill_module) {
				til_setup_free(&setup->til_setup);
				return -EINVAL;
			}

			r = til_module_setup_finalize(setup->fill_module, fill_module_settings, &setup->fill_module_setup);
			if (r < 0) {
				til_setup_free(&setup->til_setup);
				return r;
			}
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	roto_module = {
	.create_context = roto_create_context,
	.destroy_context = roto_destroy_context,
	.prepare_frame = roto_prepare_frame,
	.render_fragment = roto_render_fragment,
	.setup = roto_setup,
	.name = "roto",
	.description = "Anti-aliased tiled texture rotation (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
