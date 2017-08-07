#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "drmsetup.h"
#include "fb.h"
#include "fps.h"
#include "rototiller.h"
#include "threads.h"
#include "util.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

#define NUM_FB_PAGES	3
/* ^ By triple-buffering, we can have a page tied up being displayed, another
 * tied up submitted and waiting for vsync, and still not block on getting
 * another page so we can begin rendering another frame before vsync.  With
 * just two pages we end up twiddling thumbs until the vsync arrives.
 */

extern rototiller_module_t	julia_module;
extern rototiller_module_t	plasma_module;
extern rototiller_module_t	roto32_module;
extern rototiller_module_t	roto64_module;
extern rototiller_module_t	ray_module;
extern rototiller_module_t	sparkler_module;
extern rototiller_module_t	stars_module;

static rototiller_module_t	*modules[] = {
	&roto32_module,
	&roto64_module,
	&ray_module,
	&sparkler_module,
	&stars_module,
	&plasma_module,
	&julia_module,
};


static void module_select(int *module)
{
	int	i;

	printf("\nModules\n");
	for (i = 0; i < nelems(modules); i++) {
		printf(" %i: %s - %s\n", i, modules[i]->name, modules[i]->description);
	}

	ask_num(module, nelems(modules) - 1, "Select module", 0);
}


static void module_render_page_threaded(rototiller_module_t *module, void *context, threads_t *threads, fb_page_t *page)
{
	rototiller_frame_t	frame;

	module->prepare_frame(context, threads_num_threads(threads), &page->fragment, &frame);

	threads_frame_submit(threads, &frame, module->render_fragment, context);
	threads_wait_idle(threads);
}


static void module_render_page(rototiller_module_t *module, void *context, threads_t *threads, fb_page_t *page)
{
	if (!module->prepare_frame)
		return module->render_fragment(context, &page->fragment);

	module_render_page_threaded(module, context, threads, page);
}


int main(int argc, const char *argv[])
{
	int			drm_fd;
	drmModeModeInfoPtr	drm_mode;
	uint32_t		drm_crtc_id;
	uint32_t		drm_connector_id;
	threads_t		*threads;
	int			module;
	fb_t			*fb;
	void			*context = NULL;

	drm_setup(&drm_fd, &drm_crtc_id, &drm_connector_id, &drm_mode);
	module_select(&module);

	pexit_if(!(fb = fb_new(drm_fd, drm_crtc_id, &drm_connector_id, 1, drm_mode, NUM_FB_PAGES)),
		"unable to create fb");

	pexit_if(!fps_setup(),
		"unable to setup fps counter");

	exit_if(modules[module]->create_context &&
		!(context = modules[module]->create_context()),
		"unable to create module context");

	pexit_if(!(threads = threads_create()),
		"unable to create threads");

	for (;;) {
		fb_page_t	*page;

		fps_print(fb);

		page = fb_page_get(fb);
		module_render_page(modules[module], context, threads, page);
		fb_page_put(fb, page);
	}

	threads_destroy(threads);

	if (context)
		modules[module]->destroy_context(context);

	fb_free(fb);
	close(drm_fd);

	return EXIT_SUCCESS;
}
