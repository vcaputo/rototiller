#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"

#include "txt/txt.h"

/* Copyright (C) 2023 Vito Caputo <vcaputo@pengaru.com> */

/* This is intended primarily for diagnostic purposes or as a stand-in you'd eventually
 * replace with something a more visually interesting font/style.
 *
 * TODO:
 *	- Wire up taps for the x/y coordinates, making this a handy taps diagnostic
 *
 *      - Maybe add a dynamic justification mode where the h/v alignment offsets are
 *        just the inverse of the normalized x/y coordinates.  This requires extending
 *        libs/txt to support precise offsetting when rendering as an alternative to the
 *        enum'd txt_align_t variant.  But it would allow a tapped x/y coordinate user to
 *        sweep the coordinates edge-to-edge with the text smoothly adjusting its offset
 *        throughout the sweep so it doesn't extend off-screen at the nearest edge.
 */

#define	ASC_DEFAULT_STRING		"Hello rototiller!"
#define ASC_DEFAULT_HALIGN		TXT_HALIGN_CENTER
#define ASC_DEFAULT_VALIGN		TXT_VALIGN_CENTER
#define ASC_DEFAULT_X			0
#define ASC_DEFAULT_Y			0


typedef struct asc_setup_t {
	til_setup_t	til_setup;

	const char	*string;
	txt_halign_t	halign;
	txt_valign_t	valign;
	float		x, y;
} asc_setup_t;

typedef struct asc_context_t {
	til_module_context_t	til_module_context;

	txt_t			*txt;
} asc_context_t;


static til_module_context_t * asc_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	asc_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(asc_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->txt = txt_new(((asc_setup_t *)setup)->string);
	if (!ctxt->txt)
		return til_module_context_free(&ctxt->til_module_context);

	return &ctxt->til_module_context;
}


static void asc_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	asc_context_t		*ctxt = (asc_context_t *)context;
	asc_setup_t		*s = (asc_setup_t *)context->setup;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	til_fb_fragment_clear(fragment);

	txt_render_fragment(ctxt->txt, fragment, 0xffffffff,
			    s->x * ((float)fragment->frame_width) * .5f + .5f * ((float)fragment->frame_width),
			    s->y * ((float)fragment->frame_height) * .5f + .5f * ((float)fragment->frame_height),
			    (txt_align_t){
				.horiz = s->halign,
				.vert = s->valign
			    });
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
	til_setting_t	*valign;
	til_setting_t	*halign;
	til_setting_t	*x, *y;
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

		r = til_value_to_pos(halign_values, halign->value, (unsigned *)&setup->halign);
		if (r < 0)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, halign, res_setting, -EINVAL);

		r = til_value_to_pos(valign_values, valign->value, (unsigned *)&setup->valign);
		if (r < 0)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, valign, res_setting, -EINVAL);

		if (sscanf(x->value, "%f", &setup->x) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, x, res_setting, -EINVAL);

		if (sscanf(y->value, "%f", &setup->y) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, y, res_setting, -EINVAL);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
