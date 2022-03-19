#ifndef _TIL_H
#define _TIL_H

#include "til_fb.h"

/* til_fragmenter produces fragments from an input fragment, num being the desired fragment for the current call.
 * return value of 1 means a fragment has been produced, 0 means num is beyond the end of fragments. */
typedef int (*til_fragmenter_t)(void *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment);

typedef struct til_settings_t settings;
typedef struct til_setting_desc_t til_setting_desc_t;
typedef struct til_knob_t til_knob_t;

typedef struct til_module_t {
	void *	(*create_context)(unsigned ticks, unsigned n_cpus);
	void	(*destroy_context)(void *context);
	void	(*prepare_frame)(void *context, unsigned ticks, unsigned n_cpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter);
	void	(*render_fragment)(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment);
	void	(*finish_frame)(void *context, unsigned ticks, til_fb_fragment_t *fragment);
	int	(*setup)(const til_settings_t *settings, const til_setting_t **res_setting, const til_setting_desc_t **res_desc);
	size_t	(*knobs)(void *context, til_knob_t **res_knobs);
	char	*name;
	char	*description;
	char	*author;
} til_module_t;

int til_init(void);
void til_quiesce(void);
void til_shutdown(void);
const til_module_t * til_lookup_module(const char *name);
void til_get_modules(const til_module_t ***res_modules, size_t *res_n_modules);
void til_module_render(const til_module_t *module, void *context, unsigned ticks, til_fb_fragment_t *fragment);
int til_module_create_context(const til_module_t *module, unsigned ticks, void **res_context);
int til_module_setup(til_settings_t *settings, const til_setting_t **res_setting, const til_setting_desc_t **res_desc);

#endif
