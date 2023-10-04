#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_stream.h"
#include "til_tap.h"

#include "txt/txt.h"

/* Copyright (C) 2023 Vito Caputo <vcaputo@pengaru.com> */

/* This is intended primarily for diagnostic purposes or as a stand-in you'd eventually
 * replace with something a more visually interesting font/style.
 */

#define	ASC_DEFAULT_STRING		"Hello rototiller!"
#define ASC_DEFAULT_JUSTIFY		ASC_JUSTIFY_ALIGNED
#define ASC_DEFAULT_HALIGN		TXT_HALIGN_CENTER
#define ASC_DEFAULT_VALIGN		TXT_VALIGN_CENTER
#define ASC_DEFAULT_HOFFSET		"auto"
#define ASC_DEFAULT_VOFFSET		"auto"
#define ASC_DEFAULT_X			0
#define ASC_DEFAULT_Y			0


typedef enum asc_justify_t {
	ASC_JUSTIFY_ALIGNED,
	ASC_JUSTIFY_OFFSETTED,
	ASC_JUSITFY_CNT
} asc_justify_t;

typedef struct asc_setup_t {
	til_setup_t	til_setup;

	const char	*string;
	asc_justify_t	justify;
	txt_halign_t	halign, valign;
	float		hoffset, voffset;
	float		x, y;
} asc_setup_t;

typedef struct asc_context_t {
	til_module_context_t	til_module_context;

	struct {
		til_tap_t		x, y;
		til_tap_t		hoffset, voffset;
	}			taps;

	struct {
		float			x, y;
		float			hoffset, voffset;
	}			vars;

	float			*x, *y;
	float			*hoffset, *voffset;

	txt_t			*txt;
} asc_context_t;


static void asc_update_taps(asc_context_t *ctxt, til_stream_t *stream)
{
	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.x))
		*ctxt->x = ((asc_setup_t *)ctxt->til_module_context.setup)->x;
	else
		ctxt->vars.x = *ctxt->x;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.y))
		*ctxt->y = ((asc_setup_t *)ctxt->til_module_context.setup)->y;
	else
		ctxt->vars.y = *ctxt->y;

	/* XXX: maybe clamp to -1.0..+1.0 ?  It's not a crash risk since txt_render_fragment_aligned()
	 * clips to the fragment by using ...put_pixel_checked() *shrug*
	 */

	if (((asc_setup_t *)ctxt->til_module_context.setup)->justify != ASC_JUSTIFY_OFFSETTED)
		return;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.hoffset))
		*ctxt->hoffset = ((asc_setup_t *)ctxt->til_module_context.setup)->hoffset;
	else
		ctxt->vars.hoffset = *ctxt->hoffset;

	if (!til_stream_tap_context(stream, &ctxt->til_module_context, NULL, &ctxt->taps.voffset))
		*ctxt->voffset = ((asc_setup_t *)ctxt->til_module_context.setup)->voffset;
	else
		ctxt->vars.voffset = *ctxt->voffset;
}


static til_module_context_t * asc_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	asc_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(asc_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->txt = txt_new(((asc_setup_t *)setup)->string);
	if (!ctxt->txt)
		return til_module_context_free(&ctxt->til_module_context);

	ctxt->taps.x = til_tap_init_float(ctxt, &ctxt->x, 1, &ctxt->vars.x, "x");
	ctxt->taps.y = til_tap_init_float(ctxt, &ctxt->y, 1, &ctxt->vars.y, "y");

	if (((asc_setup_t *)setup)->justify == ASC_JUSTIFY_OFFSETTED) {
		ctxt->taps.hoffset = til_tap_init_float(ctxt, &ctxt->hoffset, 1, &ctxt->vars.hoffset, "hoffset");
		ctxt->taps.voffset = til_tap_init_float(ctxt, &ctxt->voffset, 1, &ctxt->vars.voffset, "voffset");
	}

	asc_update_taps(ctxt, stream);

	return &ctxt->til_module_context;
}


static void asc_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	asc_context_t		*ctxt = (asc_context_t *)context;
	asc_setup_t		*s = (asc_setup_t *)context->setup;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	asc_update_taps(ctxt, stream);

	til_fb_fragment_clear(fragment);

	switch (s->justify) {
	case ASC_JUSTIFY_ALIGNED:
		return txt_render_fragment_aligned(ctxt->txt, fragment, 0xffffffff,
						   ctxt->vars.x * ((float)fragment->frame_width) * .5f + .5f * ((float)fragment->frame_width),
						   ctxt->vars.y * ((float)fragment->frame_height) * .5f + .5f * ((float)fragment->frame_height),
						   (txt_align_t){
							.horiz = s->halign,
							.vert = s->valign
						   });

	case ASC_JUSTIFY_OFFSETTED: {
		float	hoffset = ctxt->vars.hoffset,
			voffset = ctxt->vars.voffset;

		if (isnan(hoffset))
			hoffset = ctxt->vars.x;

		if (isnan(voffset))
			voffset = ctxt->vars.y;

		return txt_render_fragment_offsetted(ctxt->txt, fragment, 0xffffffff,
						     ctxt->vars.x * ((float)fragment->frame_width) * .5f + .5f * ((float)fragment->frame_width),
						     ctxt->vars.y * ((float)fragment->frame_height) * .5f + .5f * ((float)fragment->frame_height),
						     hoffset, voffset);
	}

	default:
		assert(0);
	}
}


static void asc_setup_free(til_setup_t *setup)
{
	asc_setup_t	*s = (asc_setup_t *)setup;

	if (s) {
		free((void *)s->string);
		free(s);
	}
}


static int asc_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	asc_module = {
	.create_context = asc_create_context,
	.render_fragment = asc_render_fragment,
	.setup = asc_setup,
	.name = "asc",
	.description = "ASCII text",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.flags = TIL_MODULE_OVERLAYABLE,
};


static int asc_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*string;
	til_setting_t	*justify;
	til_setting_t	*valign, *voffset;
	til_setting_t	*halign, *hoffset;
	til_setting_t	*x, *y;
	const char	*justify_values[] = {
				"aligned",
				"offsetted",
				NULL
			};
	const char	*valign_values[] = {
				"center",
				"top",
				"bottom",
				NULL
			};
	const char	*halign_values[] = {
				"center",
				"left",
				"right",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Text string",
							.key = "string",
							/* .regex = "" TODO */
							.preferred = ASC_DEFAULT_STRING,
						},
						&string,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Justification",
							.key = "justify",
							/* .regex = "" TODO */
							.preferred = justify_values[ASC_DEFAULT_JUSTIFY],
							.values = justify_values,
							.annotations = NULL
						},
						&justify,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (!strcasecmp(justify->value, justify_values[ASC_JUSTIFY_ALIGNED])) {
		r = til_settings_get_and_describe_setting(settings,
							&(til_setting_spec_t){
								.name = "Vertical alignment",
								.key = "valign",
								/* .regex = "" TODO */
								.preferred = valign_values[ASC_DEFAULT_VALIGN],
								.values = valign_values,
								.annotations = NULL
							},
							&valign,
							res_setting,
							res_desc);
		if (r)
			return r;

		r = til_settings_get_and_describe_setting(settings,
							&(til_setting_spec_t){
								.name = "Horizontal alignment",
								.key = "halign",
								/* .regex = "" TODO */
								.preferred = halign_values[ASC_DEFAULT_HALIGN],
								.values = halign_values,
								.annotations = NULL
							},
							&halign,
							res_setting,
							res_desc);
		if (r)
			return r;
	} else {
		r = til_settings_get_and_describe_setting(settings,
							&(til_setting_spec_t){
								.name = "Vertical offset [-1.0...1.0] or 'auto'",
								.key = "voffset",
								/* .regex = "" TODO */
								.preferred = ASC_DEFAULT_VOFFSET,
								.annotations = NULL
							},
							&voffset,
							res_setting,
							res_desc);
		if (r)
			return r;

		r = til_settings_get_and_describe_setting(settings,
							&(til_setting_spec_t){
								.name = "Horizontal offset [-1.0...1.0] or 'auto'",
								.key = "hoffset",
								/* .regex = "" TODO */
								.preferred = ASC_DEFAULT_HOFFSET,
								.annotations = NULL
							},
							&hoffset,
							res_setting,
							res_desc);
		if (r)
			return r;
	}

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "X coordinate [-1.0...1.0]",
							.key = "x",
							/* .regex = "" TODO */
							.preferred = TIL_SETTINGS_STR(ASC_DEFAULT_X),
							.annotations = NULL
						},
						&x,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Y coordinate [-1.0...1.0]",
							.key = "y",
							/* .regex = "" TODO */
							.preferred = TIL_SETTINGS_STR(ASC_DEFAULT_Y),
							.annotations = NULL
						},
						&y,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		asc_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), asc_setup_free, &asc_module);
		if (!setup)
			return -ENOMEM;

		setup->string = strdup(string->value);
		if (!setup->string)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, string, res_setting, -ENOMEM);

		r = til_value_to_pos(justify_values, justify->value, (unsigned *)&setup->justify);
		if (r < 0)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, justify, res_setting, -EINVAL);

		if (setup->justify == ASC_JUSTIFY_ALIGNED) {
			r = til_value_to_pos(halign_values, halign->value, (unsigned *)&setup->halign);
			if (r < 0)
				return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, halign, res_setting, -EINVAL);

			r = til_value_to_pos(valign_values, valign->value, (unsigned *)&setup->valign);
			if (r < 0)
				return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, valign, res_setting, -EINVAL);
		} else {
			if (!strcasecmp(hoffset->value, "auto"))
				setup->hoffset = NAN;
			else if (sscanf(hoffset->value, "%f", &setup->hoffset) != 1)
				return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, hoffset, res_setting, -EINVAL);

			if (!strcasecmp(voffset->value, "auto"))
				setup->voffset = NAN;
			else if (sscanf(voffset->value, "%f", &setup->voffset) != 1)
				return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, voffset, res_setting, -EINVAL);
		}

		if (sscanf(x->value, "%f", &setup->x) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, x, res_setting, -EINVAL);

		if (sscanf(y->value, "%f", &setup->y) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, y, res_setting, -EINVAL);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
