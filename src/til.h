#ifndef _TIL_H
#define _TIL_H

#include "til_fb.h"
#include "til_module_context.h"
#include "til_setup.h"

/* til_fragmenter_t produces fragments from an input fragment, num being the desired fragment for the current call.
 * return value of 1 means a fragment has been produced, 0 means num is beyond the end of fragments. */
typedef int (*til_fragmenter_t)(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment);

/* til_frame_plan_t is what til_module_t.prepare_frame() populates to return a fragmenter and any flags/rules */
typedef struct til_frame_plan_t {
	unsigned		cpu_affinity:1;	/* maintain a stable fragnum:cpu/thread mapping? (slower) */
	til_fragmenter_t	fragmenter;	/* fragmenter to use in rendering the frame */
} til_frame_plan_t;

typedef struct til_module_t til_module_t;
typedef struct til_settings_t settings;
typedef struct til_setting_desc_t til_setting_desc_t;
typedef struct til_stream_t til_stream_t;

#define TIL_MODULE_OVERLAYABLE	1u	/* module is appropriate for overlay use */
#define TIL_MODULE_HERMETIC	2u	/* module doesn't work readily with other modules / requires manual settings */
#define TIL_MODULE_EXPERIMENTAL	4u	/* module is buggy / unfinished */
#define TIL_MODULE_BUILTIN	8u	/* module is implements "built-in" libtil functionality not intended to be interesting by itself */

struct til_module_t {
	til_module_context_t *	(*create_context)(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup);
	void			(*destroy_context)(til_module_context_t *context);	/* destroy gets stream in context, but the render-related functions should always use the passed-in stream so it can potentially change */
	void			(*prepare_frame)(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan);
	void			(*render_fragment)(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr);
	void			(*finish_frame)(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr);
	int			(*setup)(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);
	char			*name;
	char			*description;
	char			*author;
	unsigned		flags;
};

int til_init(void);
void til_quiesce(void);
void til_shutdown(void);
unsigned til_ticks_now(void);
const til_module_t * til_lookup_module(const char *name);
void til_get_modules(const til_module_t ***res_modules, size_t *res_n_modules);
char * til_get_module_names(unsigned flags_excluded, const char **exclusions);
void til_module_render(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr);
void til_module_render_limited(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned max_cpus, til_fb_fragment_t **fragment_ptr);
int til_module_create_contexts(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup, size_t n_contexts, til_module_context_t **res_contexts);
int til_module_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup, til_module_context_t **res_context);
til_module_context_t * til_module_destroy_context(til_module_context_t *context, til_stream_t *stream);
int til_module_setup_full(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup, const char *name, const char *preferred, unsigned flags_excluded, const char **exclusions);
int til_module_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);
int til_module_setup_finalize(const til_module_t *module, const til_settings_t *module_settings, til_setup_t **res_setup);
int til_module_settings_randomize(const til_module_t *module, til_settings_t *settings, unsigned seed, til_setup_t **res_setup, char **res_arg);
int til_fragmenter_slice_per_cpu(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment);
int til_fragmenter_slice_per_cpu_x16(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment);
int til_fragmenter_tile64(til_module_context_t *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment);

#endif
