#ifndef _ROTOTILLER_H
#define _ROTOTILLER_H

#include "fb.h"

/* Intentionally setting this larger than any anticipated number of CPUs */
#define ROTOTILLER_FRAME_MAX_FRAGMENTS	1024

typedef struct rototiller_frame_t {
	unsigned	n_fragments;
	fb_fragment_t	fragments[ROTOTILLER_FRAME_MAX_FRAGMENTS];
} rototiller_frame_t;

typedef struct rototiller_module_t {
	void *	(*create_context)(void);
	void	(*destroy_context)(void *context);
	void	(*prepare_frame)(void *context, unsigned n_cpus, fb_fragment_t *fragment, rototiller_frame_t *res_frame);
	void	(*render_fragment)(void *context, fb_fragment_t *fragment);
	char	*name;
	char	*description;
	char	*author;
	char	*license;
} rototiller_module_t;

#endif
