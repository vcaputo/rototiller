#ifndef _DRM_FB_H
#define _DRM_FB_H

#include <stdint.h>
#include <xf86drmMode.h>

typedef struct drm_fb_t drm_fb_t;

drm_fb_t * drm_fb_new(int drm_fd, uint32_t crtc_id, uint32_t *connectors, int n_connectors, drmModeModeInfoPtr mode);
void drm_fb_free(drm_fb_t *fb);

#endif
