#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "fb.h"
#include "util.h"


/* drm fb backend, everything drm-specific in rototiller resides here. */

typedef struct drm_fb_t {
	int			drm_fd;
	uint32_t		crtc_id;
	uint32_t		*connectors;
	int			n_connectors;
	drmModeModeInfoPtr	mode;
} drm_fb_t;

typedef struct drm_fb_page_t drm_fb_page_t;

struct drm_fb_page_t {
	uint32_t		*mmap;
	size_t			mmap_size;
	uint32_t		drm_dumb_handle;
	uint32_t		drm_fb_id;
};


drm_fb_t * drm_fb_new(int drm_fd, uint32_t crtc_id, uint32_t *connectors, int n_connectors, drmModeModeInfoPtr mode)
{
	drm_fb_t	*c;

	c = calloc(1, sizeof(drm_fb_t));
	if (!c)
		return NULL;

	c->drm_fd = drm_fd;
	c->crtc_id = crtc_id;
	c->connectors = connectors;
	c->n_connectors = n_connectors;
	c->mode = mode;

	return c;
}


void drm_fb_free(drm_fb_t *context)
{
	free(context);
}


static int drm_fb_acquire(void *context, void *page)
{
	drm_fb_t	*c = context;
	drm_fb_page_t	*p = page;

	return drmModeSetCrtc(c->drm_fd, c->crtc_id, p->drm_fb_id, 0, 0, c->connectors, c->n_connectors, c->mode);
}


static void drm_fb_release(void *context)
{
	/* TODO restore the existing mode @ last acquire? */
}


static void * drm_fb_page_alloc(void *context, fb_page_t *res_page)
{
	struct drm_mode_create_dumb	create_dumb = { .bpp = 32 };
	struct drm_mode_map_dumb	map_dumb = {};
	uint32_t			*map, fb_id;
	drm_fb_t			*c = context;
	drm_fb_page_t			*p;

	p = calloc(1, sizeof(drm_fb_page_t));
	if (!p)
		return NULL;

	create_dumb.width = c->mode->hdisplay;
	create_dumb.height = c->mode->vdisplay;

	pexit_if(ioctl(c->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb) < 0,
		"unable to create dumb buffer");

	map_dumb.handle = create_dumb.handle;
	pexit_if(ioctl(c->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb) < 0,
		"unable to prepare dumb buffer for mmap");
	pexit_if(!(map = mmap(NULL, create_dumb.size, PROT_READ|PROT_WRITE, MAP_SHARED, c->drm_fd, map_dumb.offset)),
		"unable to mmap dumb buffer");
	pexit_if(drmModeAddFB(c->drm_fd, c->mode->hdisplay, c->mode->vdisplay, 24, 32, create_dumb.pitch, create_dumb.handle, &fb_id) < 0,
		"unable to add dumb buffer");

	p->mmap = map;
	p->mmap_size = create_dumb.size;
	p->drm_dumb_handle = map_dumb.handle;
	p->drm_fb_id = fb_id;

	res_page->fragment.buf = map;
	res_page->fragment.width = c->mode->hdisplay;
	res_page->fragment.frame_width = c->mode->hdisplay;
	res_page->fragment.height = c->mode->vdisplay;
	res_page->fragment.frame_height = c->mode->vdisplay;
	res_page->fragment.stride = create_dumb.pitch - (c->mode->hdisplay * 4);

	return p;
}


static int drm_fb_page_free(void *context, void *page)
{
	struct drm_mode_destroy_dumb	destroy_dumb = {};
	drm_fb_t			*c = context;
	drm_fb_page_t			*p = page;

	drmModeRmFB(c->drm_fd, p->drm_fb_id);
	munmap(p->mmap, p->mmap_size);

	destroy_dumb.handle = p->drm_dumb_handle;
	ioctl(c->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb); // XXX: errors?

	free(p);

	return 0;
}


static int drm_fb_page_flip(void *context, void *page)
{
	drmEventContext	drm_ev_ctx = {
				.version = DRM_EVENT_CONTEXT_VERSION,
				.vblank_handler = NULL,
				.page_flip_handler = NULL
			};
	drm_fb_t	*c = context;
	drm_fb_page_t	*p = page;

	if (drmModePageFlip(c->drm_fd, c->crtc_id, p->drm_fb_id, DRM_MODE_PAGE_FLIP_EVENT, NULL) < 0)
		return -1;

	return drmHandleEvent(c->drm_fd, &drm_ev_ctx);
}


fb_ops_t drm_fb_ops = {
	.acquire = drm_fb_acquire,
	.release = drm_fb_release,
	.page_alloc = drm_fb_page_alloc,
	.page_free = drm_fb_page_free,
	.page_flip = drm_fb_page_flip
};
