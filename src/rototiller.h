#ifndef _ROTOTILLER_H
#define _ROTOTILLER_H

#include "fb.h"

/* rototiller_fragmenter produces fragments from an input fragment, num being the desired fragment for the current call.
 * return value of 1 means a fragment has been produced, 0 means num is beyond the end of fragments. */
typedef int (*rototiller_fragmenter_t)(void *context, const fb_fragment_t *fragment, unsigned number, fb_fragment_t *res_fragment);

typedef struct settings_t settings;
typedef struct setting_desc_t setting_desc_t;

typedef struct rototiller_module_t {
	void *	(*create_context)(unsigned ticks, unsigned n_cpus);
	void	(*destroy_context)(void *context);
	void	(*prepare_frame)(void *context, unsigned ticks, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter);
	void	(*render_fragment)(void *context, unsigned ticks, unsigned cpu, fb_fragment_t *fragment);
	void	(*finish_frame)(void *context, unsigned ticks, fb_fragment_t *fragment);
	char	*name;
	char	*description;
	char	*author;
	char	*license;
	int	(*setup)(const settings_t *settings, setting_desc_t **next_setting);
} rototiller_module_t;

const rototiller_module_t * rototiller_lookup_module(const char *name);
void rototiller_get_modules(const rototiller_module_t ***res_modules, size_t *res_n_modules);
void rototiller_module_render(const rototiller_module_t *module, void *context, unsigned ticks, fb_fragment_t *fragment);

#endif
