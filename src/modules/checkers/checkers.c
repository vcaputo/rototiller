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
#include <stdlib.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"


#define CHECKERS_DEFAULT_SIZE		32
#define CHECKERS_DEFAULT_PATTERN	CHECKERS_PATTERN_CHECKERED
#define CHECKERS_DEFAULT_DYNAMICS	CHECKERS_DYNAMICS_ODD
#define CHECKERS_DEFAULT_DYNAMICS_RATE	1.0
#define CHECKERS_DEFAULT_FILL		CHECKERS_FILL_COLOR
#define CHECKERS_DEFAULT_FILL_COLOR	0xffffff
#define CHECKERS_DEFAULT_FILL_MODULE	"none"
#define CHECKERS_DEFAULT_CLEAR		CHECKERS_CLEAR_CLEAR
#define CHECKERS_DEFAULT_CLEAR_COLOR	0x000000


typedef enum checkers_pattern_t {
	CHECKERS_PATTERN_CHECKERED,
	CHECKERS_PATTERN_RANDOM,
} checkers_pattern_t;

typedef enum checkers_dynamics_t {
	CHECKERS_DYNAMICS_ODD,
	CHECKERS_DYNAMICS_EVEN,
	CHECKERS_DYNAMICS_ALTERNATING,
	CHECKERS_DYNAMICS_RANDOM,
} checkers_dynamics_t;

typedef enum checkers_fill_t {
	CHECKERS_FILL_COLOR,
	CHECKERS_FILL_SAMPLED,
	CHECKERS_FILL_TEXTURED,
	CHECKERS_FILL_RANDOM, /* position matters here; randomizes within above values */
	CHECKERS_FILL_MIXED, /* XXX: not yet implemented, synonym for random */
} checkers_fill_t;

typedef enum checkers_clear_t {
	CHECKERS_CLEAR_CLEAR,
	CHECKERS_CLEAR_COLOR,
	CHECKERS_CLEAR_SAMPLED,
	CHECKERS_CLEAR_TEXTURED,
	CHECKERS_CLEAR_RANDOM, /* position matters here; randomizes within above values */
	CHECKERS_CLEAR_MIXED, /* XXX: not yet implemented, synonym for random */
} checkers_clear_t;

typedef struct checkers_setup_t {
	til_setup_t		til_setup;
	unsigned		size;
	checkers_pattern_t	pattern;
	checkers_dynamics_t	dynamics;
	float			rate;

	checkers_fill_t		fill;
	uint32_t		fill_color;
	til_setup_t		*fill_module_setup;

	checkers_clear_t	clear;
	uint32_t		clear_color;
} checkers_setup_t;

typedef struct checkers_context_t {
	til_module_context_t	til_module_context;
	checkers_setup_t	*setup;
	til_fb_fragment_t	waste_fb;
	til_module_context_t	*fill_module_contexts[];
} checkers_context_t;


static til_module_context_t * checkers_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	size_t			size = sizeof(checkers_context_t);
	checkers_context_t	*ctxt;

	if (((checkers_setup_t *)setup)->fill_module_setup)
		size += sizeof(til_module_context_t *) * n_cpus;

	ctxt = til_module_context_new(module, size, stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->setup = (checkers_setup_t *)setup;

	if (ctxt->setup->fill_module_setup) {
		const til_module_t	*module = ctxt->setup->fill_module_setup->creator;
		unsigned		waste_fb_size = ctxt->setup->size;

		/* since checkers is already threaded, create an n_cpus=1 context per-cpu */
		if (til_module_create_contexts(module, stream, seed, ticks, 1, ctxt->setup->fill_module_setup, n_cpus, ctxt->fill_module_contexts) < 0)
			return til_module_context_free(&ctxt->til_module_context);

		/* XXX: you might be wondering: why not just use a single context, render into a tile-sized frame buffer in prepare_frame(),
		 * and just copy from that into the filled cells in threaded render_fragment()?  it would avoid all this context clones
		 * sharing the same path complexity... and if the copy applied alpha/transparency, you'd still get overlay effects.
		 * What gives?
		 *
		 * The answer is: overlay effects go beyond simple transparencies.  With fragment snapshotting, arbitrary sampling of
		 * anywhere within the incoming fragment's contents while writing the fragment is facilitated, so you get use cases like
		 * drizzle,style=map which treat the "puddle" as a sort of normal map and read in pixel colors from wherever the normal
		 * aims.  And there's sampler overlays like voronoi and checkers, which fill arbitrary regions using the incoming color
		 * from an arbitrary pixel.   For any of these, rendering once into an intermediate single tile-sized frame buffer would
		 * sample the blank tile-sized frame buffer, so they'd just be blank.  It's more like what they'd need is to store the
		 * relative coordinates to read from per-pixel, instead of the final pixel data, in the tile-sized frame buffer, combined
		 * with encoding what operation to do on what was read.  Instead of that complexity, I'm just cloning the contexts with
		 * identical seed+settings, and running them per-cpu.  It may be interesting to experiment with storing source coordinates
		 * with color+operation information in a different "sampler" type of frame...  but all these modules would need to then
		 * learn to store that instead of just always producing pixels.  It'd be a substantial invasive change, and would constrain
		 * modules to work within what that sampler frame could express, whereas this context cloning lets the module authors just
		 * implement whatever they want for producing pixels.
		 *
		 * It might seem like a lot of trouble just for a silly checkers module, but ensuring this works properly for checkers
		 * removes the friction of doing the same kind of fill_module= instancing for concurrency in any future modules.
		 *
		 * TODO: a remaining problem to address surrounding context clones is the "ref" builtin, and til_stream_find_module_contexts()
		 * which it relies on.  Imagine checkers uses fill_module=ref,path=/path/to/context/without/clones with things as-is.
		 * There would be n_cpus ref contexts in checkers, and each of those rendering concurrently will find the same single context
		 * at /path/to/context/without/clones on the stream.  So they'd racily render while sharing a single context, which might
		 * crash - depending on what module that context belongs to.  What needs to happen here is when the "ref" clones lookup
		 * the context @ path to use, they need a clone number to propagate down to til_stream_find_module_contexts(), which
		 * would have been assigned when the n_cpus ref clones were created for checkers.  Then til_stream_find_module_contexts()
		 * can ensure the context returned has the same clone number within the found path.  But there's still a need to actually
		 * create the clones when they don't exist.  Keep in mind, the /path/to/context/without/clones will have just a
		 * single context, and we're going to want n_cpus of them from the same path via "ref".  So the one that exists needs to be
		 * cloned on demand, in a race-free manner likely requiring some locking, and it will require the cloned context's module
		 * to perform the actual cloning in all but the simples of cases.  So it looks like til_module_t.clone_context() is needed,
		 * and should be asserted as present when til_module_t.destroy_context() is present (and vice versa), as the presence of
		 * either strongly implies the context refers to external resources.  And in the absence of a clone_context() method, we
		 * can have a default "dumb" clone that just duplicates the context with a malloc() and memcpy().  This would also require
		 * adding a size member to til_module_context_t, which is straightforward to add.
		 */

		 ctxt->waste_fb = (til_fb_fragment_t){
					.buf = malloc(waste_fb_size * waste_fb_size * sizeof(uint32_t)),
					.frame_width = waste_fb_size,
					.frame_height = waste_fb_size,
					.width = waste_fb_size,
					.height = waste_fb_size,
					.pitch = waste_fb_size,
				  };
		if (!ctxt->waste_fb.buf)
			return til_module_context_free(&ctxt->til_module_context);
	}

	return &ctxt->til_module_context;
}


static void checkers_destroy_context(til_module_context_t *context)
{
	checkers_context_t	*ctxt = (checkers_context_t *)context;

	if (ctxt->setup->fill_module_setup) {
		for (unsigned i = 0; i < context->n_cpus; i++)
			til_module_context_free(ctxt->fill_module_contexts[i]);

		free(ctxt->waste_fb.buf);
	}

	free(ctxt);
}


/* This is derived from til_fb_fragment_tile_single() with two variations:
 * 1. when the size doesn't align with frame size, the start tiles are offset
 *    to center the checkers letting the edge checkers all clip as needed
 * 2. the incoming frame width isn't propagated down to the tiled fragments,
 *    though for the potentially clipped boundary tiles the frame_{width,height}
 *    won't match the incoming width,height.
 *
 * XXX note this fragmenter in particular really exercises fill_modules' correct handling
 *     of frame vs. fragment dimensions and clipping semantics
 */
int checkers_fragment_tile_single(const til_fb_fragment_t *fragment, unsigned tile_size, unsigned number, til_fb_fragment_t *res_fragment)
{
	unsigned	numw = fragment->width / tile_size, numh = fragment->height / tile_size;
	unsigned	x, y, xoff, yoff, xshift = 0, yshift = 0;

	assert(fragment);
	assert(res_fragment);

	{
		unsigned	tiled_w = numw * tile_size;
		unsigned	tiled_h = numh * tile_size;

		/* Detect the need for fractional tiles on both axis and shift the fragments
		 * to keep the overall checkered output centered.
		 *
		 * This complicates res_fragment.{x,y,width,height} calculations for the
		 * peripheral checker tiles as those must clip when shifted.
		 */

		if (tiled_w < fragment->width) {
			tiled_w += tile_size;
			xshift = (tiled_w - fragment->width) >> 1;
			numw++;
		}

		if (tiled_h < fragment->height) {
			tiled_h += tile_size;
			yshift = (tiled_h - fragment->height) >> 1;
			numh++;
		}
	}

	y = number / numw;
	if (y >= numh)
		return 0;

	x = number - (y * numw);

	xoff = x * tile_size;
	yoff = y * tile_size;

	if (fragment->texture) {
		assert(res_fragment->texture);
		assert(fragment->frame_width == fragment->texture->frame_width);
		assert(fragment->frame_height == fragment->texture->frame_height);
		assert(fragment->width == fragment->texture->width);
		assert(fragment->height == fragment->texture->height);
		assert(fragment->x == fragment->texture->x);
		assert(fragment->y == fragment->texture->y);

		*(res_fragment->texture) = (til_fb_fragment_t){
					.buf = fragment->texture->buf + (yoff * fragment->texture->pitch) - (y ? (yshift * fragment->texture->pitch) : 0) + (xoff - (x ? xshift : 0)),
					.width = MIN(fragment->width - xoff + (x ? xshift : 0), x ? tile_size : (tile_size - xshift)),
					.height = MIN(fragment->height - yoff + (y ? yshift : 0), y ? tile_size : (tile_size - yshift)),
					.x = x ? 0 : xshift,
					.y = y ? 0 : yshift,
					.frame_width = tile_size,
					.frame_height = tile_size,
					.stride = fragment->texture->stride + (fragment->width - MIN(fragment->width - xoff + (x ? xshift : 0), x ? tile_size : (tile_size - xshift))),
					.pitch = fragment->texture->pitch,
					.cleared = fragment->texture->cleared,
				};
	}

	*res_fragment = (til_fb_fragment_t){
				.texture = fragment->texture ? res_fragment->texture : NULL,
				/* TODO: copy pasta! */
				.buf = fragment->buf + (yoff * fragment->pitch) - (y ? (yshift * fragment->pitch) : 0) + (xoff - (x ? xshift : 0)),
				.width = MIN(fragment->width - xoff + (x ? xshift : 0), x ? tile_size : (tile_size - xshift)),
				.height = MIN(fragment->height - yoff + (y ? yshift : 0), y ? tile_size : (tile_size - yshift)),
				.x = x ? 0 : xshift,
				.y = y ? 0 : yshift,
				// this is a little janky but leave frame_width to be set by render_fragment
				// so it can use the old frame_width for determining checkered state
				.frame_width = fragment->width, // becomes tile_size
				.frame_height = fragment->height, // becomes tile_size
				.stride = fragment->stride + (fragment->width - MIN(fragment->width - xoff + (x ? xshift : 0), x ? tile_size : (tile_size - xshift))),
				.pitch = fragment->pitch,
				.number = number,
				.cleared = fragment->cleared,
			};

	assert(res_fragment->width <= fragment->width);
	assert(res_fragment->height <= fragment->height);

#if 0
	fprintf(stderr, "incoming frame=%ux%u frag=%ux%u @=%ux%u, res frame=%ux%u fragwh=%ux%u fragxy=%ux%u off=%ux%u shift=%ux%u xy=%ux%u tsz=%u\n",
			fragment->frame_width,
			fragment->frame_height,
			fragment->width,
			fragment->height,
			fragment->x,
			fragment->y,
			res_fragment->frame_width,
			res_fragment->frame_height,
			res_fragment->width,
			res_fragment->height,
			res_fragment->x,
			res_fragment->y,
			xoff,
			yoff,
			xshift,
			yshift,
			x,
			y,
			tile_size);
#endif

	return 1;
}


static int checkers_fragmenter(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	checkers_context_t	*ctxt = (checkers_context_t *)context;

	return checkers_fragment_tile_single(fragment, ctxt->setup->size, number, res_fragment);
}


static void checkers_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	/* XXX: once upon a time this used .cpu_affinity = 1, to
	 * get a stable mapping of per-cpu-context to filled cell
	 * via/fill_module.  But that slows things down
	 * substantially as-implemented in til_threads since it
	 * makes threads wait for their next fragnum to come up.
	 * Since seed-ifying everything, and the seed is the same
	 * across the contexts, they _should_ have identical
	 * outputs.  So I got rid of it for the perf gain, and
	 * modules which aren't behaving well WRT
	 * same-seeds-same-settings-but-different-output, fix
	 * them.
	 */
	*res_frame_plan = (til_frame_plan_t){ .fragmenter = checkers_fragmenter };
}


static inline unsigned hash(unsigned x)
{
	x = ((x >> 16) ^ x) * 0x61C88647;
	x = ((x >> 16) ^ x) * 0x61C88647;
	x = ((x >> 16) ^ x) * 0x61C88647;
	x = (x >> 16) ^ x;

	return x;
}


static void checkers_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	checkers_context_t	*ctxt = (checkers_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	uint32_t		fill_color = ctxt->setup->fill_color, fill_flags = 0;
	checkers_fill_t		fill = ctxt->setup->fill;
	uint32_t		clear_color = ctxt->setup->clear_color, clear_flags = 0;
	checkers_clear_t	clear = ctxt->setup->clear;
	int			state;

	switch (ctxt->setup->pattern) {
	case CHECKERS_PATTERN_CHECKERED: {
		unsigned	tiles_per_row, row, col;

		tiles_per_row = fragment->frame_width / ctxt->setup->size;
		if (tiles_per_row * ctxt->setup->size < fragment->frame_width)
			tiles_per_row++;

		row = fragment->number / tiles_per_row;
		col = fragment->number % tiles_per_row;
		state = (row ^ col) & 0x1;
		break;
	}
	case CHECKERS_PATTERN_RANDOM:
		state = hash((context->seed + fragment->number) * 0x61C88647) & 0x1;
		break;
	default:
		assert(0);
	}

	/* now that state has been determined, set the frame size */
	fragment->frame_width = ctxt->setup->size;
	fragment->frame_height = ctxt->setup->size;
	if (fragment->texture) {
		fragment->texture->frame_width = ctxt->setup->size;
		fragment->texture->frame_height = ctxt->setup->size;
	}

	switch (ctxt->setup->dynamics) {
	case CHECKERS_DYNAMICS_ODD:
		break;
	case CHECKERS_DYNAMICS_EVEN:
		state = ~state & 0x1;
		break;
	case CHECKERS_DYNAMICS_ALTERNATING:
		state ^= ((unsigned)((float)ticks * ctxt->setup->rate) & 0x1);
		break;
	case CHECKERS_DYNAMICS_RANDOM: /* note: the big multiply here is just to get up out of the low bits */
		state &= hash((context->seed + fragment->number) * 0x61C88647 + (unsigned)((float)ticks * ctxt->setup->rate)) & 0x1;
		break;
	default:
		assert(0);
	}

	if (fill == CHECKERS_FILL_RANDOM || fill == CHECKERS_FILL_MIXED)
		fill = rand_r(&ctxt->til_module_context.seed) % CHECKERS_FILL_RANDOM; /* TODO: mixed should have a setting for controlling the ratios */

	if (clear == CHECKERS_CLEAR_RANDOM || clear == CHECKERS_CLEAR_MIXED)
		clear = rand_r(&ctxt->til_module_context.seed) % CHECKERS_CLEAR_RANDOM; /* TODO: mixed should have a setting for controlling the ratios */

	switch (ctxt->setup->fill) {
	case CHECKERS_FILL_SAMPLED:
		if (fragment->cleared)
			fill_color = til_fb_fragment_get_pixel_unchecked(fragment, fragment->x + (fragment->width >> 1), fragment->y + (fragment->height >> 1));
		break;
	case CHECKERS_FILL_TEXTURED:
		fill_flags = TIL_FB_DRAW_FLAG_TEXTURABLE;
		break;
	case CHECKERS_FILL_COLOR:
	default:
		break;
	}

	switch (ctxt->setup->clear) {
	case CHECKERS_CLEAR_SAMPLED:
		if (fragment->cleared)
			clear_color = til_fb_fragment_get_pixel_unchecked(fragment, fragment->x + (fragment->width >> 1), fragment->y + (fragment->height >> 1));
		break;
	case CHECKERS_CLEAR_TEXTURED:
		clear_flags = TIL_FB_DRAW_FLAG_TEXTURABLE;
		break;
	case CHECKERS_CLEAR_COLOR:
	default:
		break;
	}

	if (!state)
		if (ctxt->setup->clear == CHECKERS_CLEAR_CLEAR)
			til_fb_fragment_clear(fragment);
		else
			til_fb_fragment_fill(fragment, clear_flags, clear_color);
		/* TODO: clear_module might be interesting too, but sort out the context sets @ path first  */
	else {
		if (!ctxt->setup->fill_module_setup)
			til_fb_fragment_fill(fragment, fill_flags, fill_color);
		else /* TODO: we need a way to send down color and flags, and use the module render as a brush of sorts */
			til_module_render(ctxt->fill_module_contexts[cpu], stream, ticks, fragment_ptr);
	}
}


static void checkers_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	checkers_context_t	*ctxt = (checkers_context_t *)context;

	if (ctxt->setup->fill_module_setup) {
		for (unsigned i = 0; i < context->n_cpus; i++) {
			til_fb_fragment_t	*waste_fb_ptr = &ctxt->waste_fb;

			if (ctxt->fill_module_contexts[i]->last_ticks == ticks)
				continue;

			/* We need to do a waste render to keep this context in sync with the others,
			 * sometimes contexts don't get to participate in threaded rendering due to
			 * scheduling irregularities/bad luck...  but we can't let them diverge.
			 *
			 * TODO: if the modules were required to expose their simulation in a distinct
			 * til_module_t.method() we'd just perform that phase here.  And it probably makes
			 * sense to do that.  The annoying thing is, it's kind of nice to have the minimum
			 * module be just til_module_t.render_fragment() for someone wanting to just make a
			 * little graphics hack.  In some sense, til_module_t.prepare_frame() already serves
			 * as that method - but it's not required, and there's no hard rule saying even if
			 * you've got a simple non-threaded module, put your state mutation in prepare_frame().
			 * there's also no expectation currently that prepare_frame() would occur without
			 * a subsequent render_fragment/finish_frame for that frame, which could be problematic
			 * if prepare_frame is e.g. priming some state in the context that gets finished after
			 * at least one invocation of render_fragment() and/or finish_frame().
			 *
			 * So for now let's just do waste renders into an off-screen cell-sized fb with any
			 * of the contexts that didn't get used.  On the plus side, this is bounded by the
			 * number of cpus, and it's only the fraction that didn't get utilized.
			 *
			 * Checkers is the only module that does this clones stuff, but I'm just using it as
			 * a development mule for this complicated case at this point.  I can imagine other
			 * modules wanting to do the same thing with concurrent clones so it's worth sorting
			 * out all the details.
			 */
			til_module_render(ctxt->fill_module_contexts[i], stream, ticks, &waste_fb_ptr);
#if 0
			/* This is probably an interesting thing to measure.  On a busy system, it's not surprising
			 * to have a cpu here and there not participate at all in rendering a frame.  But if the
			 * system is idle, that's kind of odd, unless there's a huge excess of cores vs. filled cells.
			 * It could also be a scheduling problem, or a rototiller threading bug/shortcoming.
			 */
			fprintf(stderr, "waste render @ ticks=%u cpu=%i\n", ticks, i);
#endif
		}
	}
}


/* TODO: migrate to libtil */
static char * checkers_random_color(unsigned seed)
{
	/* til should probably have a common randomize color helper for this with a large collection of
	 * reasonable colors, and maybe even have themed palettes one can choose from... */
	static const char *	colors[] = {
					"#ffffff",
					"#ff0000",
					"#00ff00",
					"#0000ff",
					"#ffff00",
					"#00ffff",
					"#ff00ff",
				};

	return strdup(colors[seed % nelems(colors)]);
}


/* TODO: migrate to libtil */
static int checkers_rgb_to_uint32(const char *in, uint32_t *out)
{
	uint32_t	color = 0;

	/* this isn't html, but accept #rrggbb syntax */
	if (*in == '#')
		in++;
	else if (in[0] == '0' && in[1] == 'x') /* and 0xrrggbb */
		in += 2;

	if (strlen(in) != 6)
		return -EINVAL;

	for (int i = 0; i < 6;) {
		uint8_t	c = 0;

		color <<= 8;

		for (int j = 0; j < 2; in++, j++, i++) {
			c <<= 4;

			switch (*in) {
			case '0'...'9':
				c |= (*in) - '0';
				break;

			case 'a'...'f':
				c |= (*in) - 'a' + 10;
				break;

			case 'A'...'F':
				c |= (*in) - 'A' + 10;
				break;

			default:
				return -EINVAL;
			}
		}

		color |= c;
	}

	*out = color;

	return 0;
}


static void checkers_setup_free(til_setup_t *setup)
{
	checkers_setup_t	*s = (checkers_setup_t *)setup;

	if (s) {
		til_setup_free(s->fill_module_setup);
		free(setup);
	}
}


/* TODO: move something like this to libtil */
static int checkers_value_to_pos(const char **options, const char *value, unsigned *res_pos)
{
	for (unsigned i = 0; options[i]; i++) {
		if (!strcasecmp(value, options[i])) {
			*res_pos = i;
			return 0;
		}
	}

	return -ENOENT;
}


static int checkers_fill_module_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	/* XXX: Note that this is for processing the underlying fill_module_settings, starting with the module name.
	 * The fill_module_values[] under checkers_setup() still dictate what the presets are for the outer fill_module= setting.
	 * So randomizers for instance will encounter the fill_module_values[] and choose from those, and what modules are available
	 * in this inner setup won't be part of the randomizer's set to draw from.  Meaning experimental and/or builtins could be
	 * allowed here without affecting randomizing, though it's kind of a hack.
	 */
	return til_module_setup_full(settings,
				     res_setting,
				     res_desc,
				     res_setup,
				     "Filled cell module name",
				     CHECKERS_DEFAULT_FILL_MODULE,
				     (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC),
				     NULL);
}


static int checkers_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	checkers_module = {
	.create_context = checkers_create_context,
	.destroy_context = checkers_destroy_context,
	.prepare_frame = checkers_prepare_frame,
	.render_fragment = checkers_render_fragment,
	.finish_frame = checkers_finish_frame,
	.setup = checkers_setup,
	.name = "checkers",
	.description = "Checker-patterned overlay (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


static int checkers_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char		*size;
	const char		*pattern;
	const char		*fill_module;
	const til_settings_t	*fill_module_settings;
	const char		*dynamics;
	const char		*dynamics_rate;
	const char		*fill, *clear;
	const char		*fill_color, *clear_color;
	const char		*size_values[] = {
					"4",
					"8",
					"16",
					"32",
					"64",
					"128",
					NULL
				};
	const char		*pattern_values[] = {
					"checkered",
					"random",
					NULL
				};
	const char		*fill_module_values[] = {
					"none",
					"blinds",
					"moire",
					"pixbounce",
					"plato",
					"roto",
					"shapes",
					"snow",
					"spiro",
					"stars",
					NULL
				};
	const char		*dynamics_values[] = {
					"odd",
					"even",
					"alternating",
					"random",
					NULL
				};
	const char		*dynamics_rate_values[] = {
					"1.0",
					".75",
					".5",
					".25",
					".1",
					".01",
					".001",
					".0001",
					NULL
				};
	const char		*fill_values[] = {
					"color",
					"sampled",
					"textured",
					"random",
					"mixed",
					NULL
				};
	const char		*clear_values[] = {
					"clear",
					"color",
					"sampled",
					"textured",
					"random",
					"mixed",
					NULL
				};
	int			r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Checker size",
							.key = "size",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(CHECKERS_DEFAULT_SIZE),
							.values = size_values,
							.annotations = NULL
						},
						&size,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Checkers pattern",
							.key = "pattern",
							.preferred = pattern_values[0],
							.values = pattern_values,
							.annotations = NULL
						},
						&pattern,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Filled cell module (\"none\" for plain checkers)",
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

	/* hmm, we're about to assume this is always valid, so let's assert that's actually the case...
	 * looking at til_settings_get_and_describe_value() it looks to be optional there.
	 * TODO: since _we_ require the res_setting, we should provide a temporary place for it
	 * here if it's not guaranteed, then copy it into *res_setting if wanted.
	 */
	assert(res_setting && *res_setting);
	assert((*res_setting)->value_as_nested_settings);

	fill_module_settings = (*res_setting)->value_as_nested_settings,

	r = checkers_fill_module_setup(fill_module_settings,
				       res_setting,
				       res_desc,
				       NULL); /* XXX: note no res_setup, must defer finalize */
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Checkers dynamics",
							.key = "dynamics",
							.preferred = dynamics_values[0],
							.values = dynamics_values,
							.annotations = NULL
						},
						&dynamics,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (strcasecmp(dynamics, "odd") && strcasecmp(dynamics, "even")) {
		r = til_settings_get_and_describe_value(settings,
							&(til_setting_spec_t){
								.name = "Checkers dynamics rate",
								.key = "dynamics_rate",
								.preferred = dynamics_rate_values[0],
								.values = dynamics_rate_values,
								.annotations = NULL
							},
							&dynamics_rate,
							res_setting,
							res_desc);
		if (r)
			return r;
	}

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Fill mode",
							.key = "fill",
							.preferred = fill_values[CHECKERS_DEFAULT_FILL],
							.values = fill_values,
							.annotations = NULL
						},
						&fill,
						res_setting,
						res_desc);
	if (r)
		return r;

	/* Even though sampled and textured fills don't neceesarily use the color,
	 * if there's no texture or no underlay to sample, we should have a color to fallback on.
	 */
	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Fill color",
							.key = "fill_color",
							.preferred = TIL_SETTINGS_STR(CHECKERS_DEFAULT_FILL_COLOR),
							.random = checkers_random_color,
							.values = NULL,
							.annotations = NULL
						},
						&fill_color,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Clear mode",
							.key = "clear",
							.preferred = clear_values[CHECKERS_DEFAULT_CLEAR],
							.values = clear_values,
							.annotations = NULL
						},
						&clear,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (strcasecmp(clear, "clear")) {
		r = til_settings_get_and_describe_value(settings,
							&(til_setting_spec_t){
								.name = "Clear color",
								.key = "clear_color",
								.preferred = TIL_SETTINGS_STR(CHECKERS_DEFAULT_CLEAR_COLOR),
								/* TODO: if it's going to randomize clear_color,
								 * it should try pick complementary ones to fill_color,
								 * disabled for now (stays the default/black)
								 * .random = checkers_random_color,
								 */
								.values = NULL,
								.annotations = NULL
							},
							&clear_color,
							res_setting,
							res_desc);
		if (r)
			return r;
	}


	if (res_setup) {
		checkers_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), checkers_setup_free, &checkers_module);
		if (!setup)
			return -ENOMEM;

		sscanf(size, "%u", &setup->size);

		if (!strcasecmp(pattern, "checkered"))
			setup->pattern = CHECKERS_PATTERN_CHECKERED;
		else if (!strcasecmp(pattern, "random"))
			setup->pattern = CHECKERS_PATTERN_RANDOM;
		else {
			til_setup_free(&setup->til_setup);
			return -EINVAL;
		}

		r = checkers_fill_module_setup(fill_module_settings,
					       res_setting,
					       res_desc,
					       &setup->fill_module_setup); /* finalize! */
		if (r < 0) {
			til_setup_free(&setup->til_setup);
			return r;
		}

		assert(r == 0);

		if (!strcasecmp(dynamics, "odd"))
			setup->dynamics = CHECKERS_DYNAMICS_ODD;
		else if (!strcasecmp(dynamics, "even"))
			setup->dynamics = CHECKERS_DYNAMICS_EVEN;
		else if (!strcasecmp(dynamics, "alternating"))
			setup->dynamics = CHECKERS_DYNAMICS_ALTERNATING;
		else if (!strcasecmp(dynamics, "random"))
			setup->dynamics = CHECKERS_DYNAMICS_RANDOM;
		else {
			til_setup_free(&setup->til_setup);
			return -EINVAL;
		}

		if (setup->dynamics != CHECKERS_DYNAMICS_ODD && setup->dynamics != CHECKERS_DYNAMICS_EVEN)
			sscanf(dynamics_rate, "%f", &setup->rate);

		r = checkers_value_to_pos(fill_values, fill, (unsigned *)&setup->fill);
		if (r < 0) {
			til_setup_free(&setup->til_setup);
			return -EINVAL;
		}

		r = checkers_rgb_to_uint32(fill_color, &setup->fill_color);
		if (r < 0) {
			til_setup_free(&setup->til_setup);
			return -EINVAL;
		}

		r = checkers_value_to_pos(clear_values, clear, (unsigned *)&setup->clear);
		if (r < 0) {
			til_setup_free(&setup->til_setup);
			return -EINVAL;
		}

		if (setup->clear != CHECKERS_CLEAR_CLEAR) {
			r = checkers_rgb_to_uint32(clear_color, &setup->clear_color);
			if (r < 0) {
				til_setup_free(&setup->til_setup);
				return -EINVAL;
			}
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}
