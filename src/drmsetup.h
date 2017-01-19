#ifndef _DRM_SETUP_H
#define _DRM_SETUP_H

#include <stddef.h>	/* xf86drmMode.h uses size_t without including stddef.h, sigh */
#include <stdint.h>
#include <xf86drmMode.h>

void drm_setup(int *res_drm_fd, uint32_t *res_crtc_id, uint32_t *res_connector_id, drmModeModeInfoPtr *res_mode);

#endif
