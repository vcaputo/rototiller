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
#include "util.h"

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

#define NUM_FB_PAGES	3
/* ^ By triple-buffering, we can have a page tied up being displayed, another
 * tied up submitted and waiting for vsync, and still not block on getting
 * another page so we can begin rendering another frame before vsync.  With
 * just two pages we end up twiddling thumbs until the vsync arrives.
 */

extern rototiller_renderer_t	plasma_renderer;
extern rototiller_renderer_t	roto32_renderer;
extern rototiller_renderer_t	roto64_renderer;
extern rototiller_renderer_t	ray_renderer;
extern rototiller_renderer_t	sparkler_renderer;
extern rototiller_renderer_t	stars_renderer;

static rototiller_renderer_t	*renderers[] = {
	&roto32_renderer,
	&roto64_renderer,
	&ray_renderer,
	&sparkler_renderer,
	&stars_renderer,
	&plasma_renderer,
};


static void renderer_select(int *renderer)
{
	int	i;

	printf("\nRenderers\n");
	for (i = 0; i < nelems(renderers); i++) {
		printf(" %i: %s - %s\n", i, renderers[i]->name, renderers[i]->description);
	}

	ask_num(renderer, nelems(renderers) - 1, "Select renderer", 0);
}


int main(int argc, const char *argv[])
{
	int			drm_fd;
	drmModeModeInfoPtr	drm_mode;
	uint32_t		drm_crtc_id;
	uint32_t		drm_connector_id;
	fb_t			*fb;
	int			renderer;

	drm_setup(&drm_fd, &drm_crtc_id, &drm_connector_id, &drm_mode);
	renderer_select(&renderer);

	pexit_if(!(fb = fb_new(drm_fd, drm_crtc_id, &drm_connector_id, 1, drm_mode, NUM_FB_PAGES)),
		"unable to create fb");

	pexit_if(!fps_setup(),
		"unable to setup fps counter");

	for (;;) {
		fb_page_t	*page;

		fps_print(fb);

		page = fb_page_get(fb);
		renderers[renderer]->render(&page->fragment);
		fb_page_put(fb, page);
	}

	fb_free(fb);
	close(drm_fd);

	return EXIT_SUCCESS;
}
