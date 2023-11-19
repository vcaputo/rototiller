#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_stream.h"

#include "libs/txt/txt.h" /* for rendering some diagnostics (ref built-in)) */


/* "blank" built-in module */
typedef struct _blank_setup_t {
	til_setup_t	til_setup;
	unsigned	force:1;
} _blank_setup_t;

static void _blank_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_slice_per_cpu };

	if (((_blank_setup_t *)context->setup)->force)
		(*fragment_ptr)->cleared = 0;
}


static void _blank_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	til_fb_fragment_clear(*fragment_ptr);
}


static int blank_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	_blank_module = {
	.prepare_frame = _blank_prepare_frame,
	.render_fragment = _blank_render_fragment,
	.setup = blank_setup,
	.name = "blank",
	.description = "Blanker (built-in)",
	.author = "built-in",
	.flags = TIL_MODULE_BUILTIN,
};


static int blank_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*values[] = {
				"off",
				"on",
				NULL
			};
	til_setting_t	*force;
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Force clearing",
							.key = "force",
							.regex = NULL,
							.preferred = values[0],
							.values = values,
							.annotations = NULL
						},
						&force,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		_blank_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &_blank_module);
		if (!setup)
			return -ENOMEM;

		if (!strcasecmp(force->value, "on"))
			setup->force = 1;

		*res_setup = &setup->til_setup;
	}

	return 0;
}


/* "noop" built-in module */
til_module_t	_noop_module = {
	.name = "noop",
	.description = "Nothing-doer (built-in)",
	.author = "built-in",
	.flags = TIL_MODULE_BUILTIN,
};


/* "ref" built-in module */
typedef struct _ref_setup_t {
	til_setup_t		til_setup;

	char			*path;
} _ref_setup_t;


typedef struct _ref_context_t {
	til_module_context_t	til_module_context;

	til_module_context_t	*ref;
} _ref_context_t;


static til_module_context_t * _ref_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	_ref_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(*ctxt), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	return &ctxt->til_module_context;
}


static void _ref_destroy_context(til_module_context_t *context)
{
	_ref_context_t	*ctxt = (_ref_context_t *)context;

	ctxt->ref = til_module_context_free(ctxt->ref);
	free(context);
}


static void _ref_render_proxy(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	_ref_context_t	*ctxt = (_ref_context_t *)context;
	_ref_setup_t	*s = (_ref_setup_t *)context->setup;

	if (!ctxt->ref) {
		int	r;

		/* TODO: switch to til_stream_find_module_context(), this clones concept is DOA. */
		r = til_stream_find_module_contexts(stream, s->path, 1, &ctxt->ref);
		if (r < 0) {
			txt_t	*msg = txt_newf("%s: BAD PATH \"%s\"", context->setup->path, s->path);

			til_fb_fragment_clear(*fragment_ptr);
			txt_render_fragment_aligned(msg, *fragment_ptr, 0xffffffff,
						    0, 0,
						    (txt_align_t){
							.horiz = TXT_HALIGN_LEFT,
							.vert = TXT_VALIGN_TOP,
						    });
			txt_free(msg);
			/* TODO: maybe print all available contexts into the fragment? */
			return;
		}
	}

	til_module_render_limited(ctxt->ref, stream, ticks, context->n_cpus, fragment_ptr);
}


static void _ref_setup_free(til_setup_t *setup)
{
	_ref_setup_t	*s = (_ref_setup_t *)setup;

	free(s->path);
	free(s);
}


static int _ref_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	_ref_module = {
	.create_context = _ref_create_context,
	.destroy_context = _ref_destroy_context,
	.render_proxy = _ref_render_proxy,
	.setup = _ref_setup,
	.name = "ref",
	.description = "Context referencer (built-in)",
	.author = "built-in",
	.flags = TIL_MODULE_BUILTIN,
};


static int _ref_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*path;
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Context path to reference",
							.key = "path",
							.regex = "[a-zA-Z0-9/_]+",
							.preferred = "",
						},
						&path,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		_ref_setup_t    *setup;

		setup = til_setup_new(settings, sizeof(*setup), _ref_setup_free, &_ref_module);
		if (!setup)
			return -ENOMEM;

		setup->path = strdup(path->value);
		if (!setup->path) {
			til_setup_free(&setup->til_setup);

			return -ENOMEM;
	       }

		*res_setup = &setup->til_setup;
	}

	return 0;
}


/* "none" built-in module */
static int _none_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	_none_module = {
	.setup = _none_setup,
	.name = "none",
	.description = "Disabled (built-in)",
	.author = "built-in",
	.flags = TIL_MODULE_BUILTIN,
};


static int _none_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	if (res_setup)
		*res_setup = NULL;

	return 0;
}


/* "pre" built-in module */
typedef struct _pre_setup_t {
	til_setup_t		til_setup;

	til_setup_t		*module_setup;
} _pre_setup_t;


typedef struct _pre_context_t {
	til_module_context_t	til_module_context;

	til_module_context_t	*module_ctxt;
} _pre_context_t;


#define _PRE_DEFAULT_MODULE	"none"


static til_module_context_t * _pre_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	_pre_setup_t	*s = (_pre_setup_t *)setup;
	_pre_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(*ctxt), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	if (s->module_setup) {
		const til_module_t	*m = s->module_setup->creator;

		if (til_module_create_context(m, stream, rand_r(&seed), ticks, n_cpus, s->module_setup, &ctxt->module_ctxt) < 0)
			return til_module_context_free(&ctxt->til_module_context);
	}

	if (til_stream_add_pre_module_context(stream, &ctxt->til_module_context) < 0)
		return til_module_context_free(&ctxt->til_module_context);

	return &ctxt->til_module_context;
}


static void _pre_destroy_context(til_module_context_t *context)
{
	_pre_context_t	*ctxt = (_pre_context_t *)context;

	til_stream_del_pre_module_context(context->stream, context);
	til_module_context_free(ctxt->module_ctxt);
	free(context);
}


static void _pre_render_proxy(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	_pre_context_t	*ctxt = (_pre_context_t *)context;

	/* TODO: introduce taps toggling the render */

	if (ctxt->module_ctxt)
		til_module_render(ctxt->module_ctxt, stream, ticks, fragment_ptr);
}


static void _pre_setup_free(til_setup_t *setup)
{
	_pre_setup_t	*s = (_pre_setup_t *)setup;

	til_setup_free(s->module_setup);
	free(s);
}


static int _pre_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_module_t	_pre_module = {
	.create_context = _pre_create_context,
	.destroy_context = _pre_destroy_context,
	.render_proxy = _pre_render_proxy,
	.setup = _pre_setup,
	.name = "pre",
	.description = "Pre-render hook registration (built-in)",
	.author = "built-in",
	.flags = TIL_MODULE_BUILTIN,
};


static int _pre_module_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	return til_module_setup_full(settings,
				     res_setting,
				     res_desc,
				     res_setup,
				     "Pre-rendering module name",
				     _PRE_DEFAULT_MODULE,
				     (TIL_MODULE_EXPERIMENTAL | TIL_MODULE_HERMETIC),
				     NULL);
}


static int _pre_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const til_settings_t	*module_settings;
	til_setting_t		*module;
	int			r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Module to hook for pre-rendering",
							.key = "module",
							.preferred = _PRE_DEFAULT_MODULE,
							.as_nested_settings = 1,
							.as_label = 1,
						},
						&module,
						res_setting,
						res_desc);
	if (r)
		return r;

	module_settings = module->value_as_nested_settings;
	assert(module_settings);

	r = _pre_module_setup(module_settings,
			      res_setting,
			      res_desc,
			      NULL); /* XXX: note no res_setup, must defer finalize */
	if (r)
		return r;

	if (res_setup) {
		_pre_setup_t    *setup;

		setup = til_setup_new(settings, sizeof(*setup), _pre_setup_free, &_pre_module);
		if (!setup)
			return -ENOMEM;

		r = _pre_module_setup(module_settings,
				      res_setting,
				      res_desc,
				      &setup->module_setup); /* finalize! */
		if (r < 0)
			return til_setup_free_with_ret_err(&setup->til_setup, r);

		assert(r == 0);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
