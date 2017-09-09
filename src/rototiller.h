#ifndef _ROTOTILLER_H
#define _ROTOTILLER_H

#include "fb.h"

/* rototiller_fragmenter produces fragments from an input fragment, num being the desired fragment for the current call.
 * return value of 1 means a fragment has been produced, 0 means num is beyond the end of fragments. */
typedef int (*rototiller_fragmenter_t)(void *context, const fb_fragment_t *fragment, unsigned num, fb_fragment_t *res_fragment);

typedef struct rototiller_module_t {
	void *	(*create_context)(void);
	void	(*destroy_context)(void *context);
	void	(*prepare_frame)(void *context, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter);
	void	(*render_fragment)(void *context, fb_fragment_t *fragment);
	char	*name;
	char	*description;
	char	*author;
	char	*license;
} rototiller_module_t;

#endif
