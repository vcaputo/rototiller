#include <math.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_stream.h"
#include "til_str.h"
#include "til_tap.h"
#include "til_util.h"

/* Copyright (C) 2023 - Vito Caputo <vcaputo@pengaru.com> */

#define BOOK_DEFAULT_PAGE_MODULE	"roto"
#define BOOK_DEFAULT_FLIP_RATE		"10"
#define BOOK_DEFAULT_FLIP_DIRECTION	"1.0"	/* negative for reverse */

/* this was derived from modules/compose.c (layers became pages) */
typedef struct book_page_t {
	til_module_context_t	*module_ctxt;
	/* XXX: it's expected that pages will get more settable attributes to stick in here */
} book_page_t;

typedef struct book_context_t {
	til_module_context_t	til_module_context;

	struct {
		til_tap_t		rate;
		til_tap_t		direction;
		til_tap_t		page;
	}			taps;

	struct {
		float			rate;
		float			direction;
		float			page;
	}			vars;

	float			*rate;
	float			*direction;
	float			*page;

	size_t			n_pages;
	book_page_t		pages[];
} book_context_t;

typedef struct book_page_setup_t {
	til_setup_t		*module_setup;
} book_page_setup_t;

typedef struct book_setup_t {
	til_setup_t		til_setup;
	float			rate;
	float			direction;
	size_t			n_pages;
	book_page_setup_t	pages[];
} book_setup_t;


static void book_update_taps(book_context_t *ctxt, til_stream_t *stream, float dt)
{
	book_setup_t	*s = (book_setup_t *)ctxt->til_module_context.setup;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.direction))
		*ctxt->direction = s->direction;
	else
		ctxt->vars.direction = *ctxt->direction;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.rate))
		*ctxt->rate = s->rate;
	else
		ctxt->vars.rate = *ctxt->rate;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.page))
		*ctxt->page = fmodf(*ctxt->page + dt * (ctxt->vars.rate * ctxt->vars.direction), ctxt->n_pages);
	else
		ctxt->vars.page = *ctxt->page;
}


static til_module_context_t * book_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	book_setup_t		*s = (book_setup_t *)setup;
	book_context_t	*ctxt;

	assert(setup);

	ctxt = til_module_context_new(module, sizeof(book_context_t) + s->n_pages * sizeof(book_page_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	/* Get the page track before any potential per-page tracks in RocketEditor. */
	ctxt->taps.page = til_tap_init_float(ctxt, &ctxt->page, 1, &ctxt->vars.page, "page");
	ctxt->taps.rate = til_tap_init_float(ctxt, &ctxt->rate, 1, &ctxt->vars.rate, "rate");
	ctxt->taps.direction = til_tap_init_float(ctxt, &ctxt->direction, 1, &ctxt->vars.direction, "direction");

	for (size_t i = 0; i < s->n_pages; i++) {
		const til_module_t	*page_module;

		/* FIXME TODO:
		 * If someone does something like pages=moire,moire,moire,moire, should they be different or the same at the seed level?
		 * As-is, this _always_ varies the seed across the pages.  There's no generic settings syntax currently for overriding
		 * that behavior where one can just specify the seed explicitly to make it the same, or even toggle this source of randomness.
		 * That's a more fundamental til thing to fix, though it could also just be a bespoke module setting to control this.
		 * Maybe for starters I should just implement it ad-hoc on a per-module basis but use the same syntax there, then if
		 * from the UX perspective that works satisfactorily, work on a more generic implementation within the settings code and
		 * deduplicate all the ad-hoc stuff from the relevant modules that will hopefully have implemented a common set of behaviors+syntax.
		 */
		page_module = s->pages[i].module_setup->creator;
		if (til_module_create_context(page_module, stream, rand_r(&seed), ticks, n_cpus, s->pages[i].module_setup, &ctxt->pages[i].module_ctxt) < 0)
			return til_module_context_free(&ctxt->til_module_context);

		ctxt->n_pages++;
	}

	book_update_taps(ctxt, stream, 0.f);

	return &ctxt->til_module_context;
}


static void book_destroy_context(til_module_context_t *context)
{
	book_context_t	*ctxt = (book_context_t *)context;

	for (size_t i = 0; i < ctxt->n_pages; i++)
		til_module_context_free(ctxt->pages[i].module_ctxt);

	free(context);
}


static void book_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	book_context_t		*ctxt = (book_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;
	size_t			i = floorf((*ctxt->page >= 0) ? *ctxt->page : ctxt->n_pages + *ctxt->page);
	float			dt = ((float)(ticks - context->last_ticks)) * .001f; 

	if (i >= ctxt->n_pages)
		i = ctxt->n_pages - 1;

	book_update_taps(ctxt, stream, dt);
	til_module_render(ctxt->pages[i].module_ctxt, stream, ticks, &fragment);

	*fragment_ptr = fragment;
}


static int book_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	book_module = {
	.create_context = book_create_context,
	.destroy_context = book_destroy_context,
	.render_fragment = book_render_fragment,
	.setup = book_setup,
	.name = "book",
	.description = "Flipbook module",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


static void book_setup_free(til_setup_t *setup)
{
	book_setup_t	*s = (book_setup_t *)setup;

	for (size_t i = 0; i < s->n_pages; i++)
		til_setup_free(s->pages[i].module_setup);

	free(setup);
}


static int book_page_module_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*exclusions[] = { "none", "book" /* XXX: prevent infinite recursion */, NULL };

	/* nested book might be interesting, but there needs to be guards to prevent the potential infinite recursion.
	 * note you can still access it via the ':' override prefix
	 */

	return til_module_setup_full(settings,
				     res_setting,
				     res_desc,
				     res_setup,
				     "Page module name",
				     BOOK_DEFAULT_PAGE_MODULE,
				     (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC | TIL_MODULE_AUDIO_ONLY),
				     exclusions);
}


static char * book_random_rate(unsigned seed)
{
	const char	*rate_values[] = {
				"60",
				"30",
				"15",
				"10",
				"5",
				"2",
				"1",
				".75",
				".5",
				".25",
				".1",
				".01",
			};

	return strdup(rate_values[rand_r(&seed) % nelems(rate_values)]);
}


static char * book_random_module_setting(unsigned seed)
{
	til_str_t	*str;
	size_t		n_pages = 2;	/* minimum of two pages */
	const char	*candidates[] = {
				"blinds",
				"checkers",
				"drizzle",
				"julia",
				"meta2d",
				"moire",
				"pixbounce",
				"plasma",
				"plato",
				"roto",
				"shapes",
				"sparkler",
				"spiro",
				"stars",
				"submit",
				"swab",
				"swarm",
				"voronoi",
			};

	str = til_str_new("");
	if (!str)
		return NULL;

	n_pages += rand_r(&seed) % 7;
	for (size_t i = 0; i < n_pages; i++) {
		size_t	c = rand_r(&seed) % nelems(candidates);

		til_str_appendf(str, "%s%s", i == 0 ? "" : ",", candidates[c]);
	}

	return til_str_to_buf(str, NULL);
}


static int book_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t		*rate;
	til_setting_t		*direction;
	const til_settings_t	*pages_settings;
	til_setting_t		*pages;
	int			r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Page flipping rate (N.N (Hz))",
							.key = "rate",
							.preferred = BOOK_DEFAULT_FLIP_RATE,
							.random = book_random_rate,
						},
						&rate,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Page flipping direction multiplier (+-N.N)",
							.key = "direction",
							.preferred = BOOK_DEFAULT_FLIP_DIRECTION,
						},
						&direction,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Comma-separated list of ordered module pages",
							.key = "pages",
							.preferred = "plasma,roto,moire",
							.annotations = NULL,
							/* TODO: .values = could have a selection of interesting preset compositions... */
							.random = book_random_module_setting,
							.as_nested_settings = 1,
						},
						&pages, /* XXX: unused in raw-value form, we want the settings instance */
						res_setting,
						res_desc);
	if (r)
		return r;

	pages_settings = pages->value_as_nested_settings;
	assert(pages_settings);
	{
		til_setting_t	*page_setting;

		/*
		 * Note this relies on til_settings_get_value_by_idx() returning NULL once idx runs off the end,
		 * which is indistinguishable from a NULL-valued setting, so if the user were to fat-finger
		 * an empty page like "pages=foo,,bar" maybe we'd never reach bar.  This could be made more robust
		 * by explicitly looking at the number of settings and just ignoring NULL values, but maybe
		 * instead we should just prohibit such settings constructions?  Like an empty value should still get
		 * "" not NULL put in it.  FIXME TODO XXX verify/clarify/assert this in code
		 */
		for (size_t i = 0; til_settings_get_value_by_idx(pages_settings, i, &page_setting); i++) {
			if (!page_setting->value_as_nested_settings) {
				r = til_setting_desc_new(pages_settings,
							 &(til_setting_spec_t){
								.as_nested_settings = 1,
							 }, res_desc);
				if (r < 0)
					return r;

				*res_setting = page_setting;

				return 1;
			}
		}

		for (size_t i = 0; til_settings_get_value_by_idx(pages_settings, i, &page_setting); i++) {
			r = book_page_module_setup(page_setting->value_as_nested_settings,
						   res_setting,
						   res_desc,
						   NULL); /* XXX: note no res_setup, must defer finalize */
			if (r)
				return r;
		}
	}

	if (res_setup) { /* turn pages settings into an array of book_page_setup_t's {name,til_setup_t} */
		size_t		n_pages = til_settings_get_count(pages_settings);
		til_setting_t	*page_setting;
		book_setup_t	*setup;

		if (n_pages < 2) {
			*res_setting = pages;

			return -EINVAL;
		}

		setup = til_setup_new(settings, sizeof(*setup) + n_pages * sizeof(*setup->pages), book_setup_free, &book_module);
		if (!setup)
			return -ENOMEM;

		if (sscanf(rate->value, "%f", &setup->rate) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, rate, res_setting, -EINVAL);

		if (setup->rate < 0)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, rate, res_setting, -EINVAL);

		if (sscanf(direction->value, "%f", &setup->direction) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, direction, res_setting, -EINVAL);

		setup->n_pages = n_pages;

		for (size_t i = 0; til_settings_get_value_by_idx(pages_settings, i, &page_setting); i++) {
			r = book_page_module_setup(page_setting->value_as_nested_settings,
						   res_setting,
						   res_desc,
						   &setup->pages[i].module_setup); /* finalize! */
			if (r < 0)
				return til_setup_free_with_ret_err(&setup->til_setup, r);

			assert(r == 0);
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}
