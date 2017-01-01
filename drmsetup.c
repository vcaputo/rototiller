/*
 * Rudimentary drm setup dialog... this is currently a very basic stdio thingy.
 */

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "util.h"

static const char * encoder_type_name(uint32_t type) {
	static const char *encoder_types[] = {
		"None",
		"DAC",
		"TMDS",
		"LVDAC",
		"VIRTUAL",
		"DSI"
	};

	assert(type < nelems(encoder_types));

	return encoder_types[type];
}


static const char * connector_type_name(uint32_t type) {
	static const char *connector_types[] = {
		"Unknown",
		"VGA",
		"DVII",
		"DVID",
		"DVIA",
		"Composite",
		"SVIDEO",
		"LVDS",
		"Component",
		"SPinDIN",
		"DisplayPort",
		"HDMIA",
		"HDMIB",
		"TV",
		"eDP",
		"VIRTUAL",
		"DSI"
	};

	assert(type < nelems(connector_types));

	return connector_types[type];
}


static const char * connection_type_name(int type) {
	static const char *connection_types[] = {
		[1] = "Connected",
		"Disconnected",
		"Unknown"
	};

	assert(type < nelems(connection_types));

	return connection_types[type];
}


/* interactively setup the drm device, store the selections */
void drm_setup(int *res_drm_fd, uint32_t *res_crtc_id, uint32_t *res_connector_id, drmModeModeInfoPtr *res_mode)
{
	int			drm_fd, i, connected;
	drmVersionPtr		drm_ver;
	drmModeResPtr		drm_res;
	drmModeConnectorPtr	drm_con;
	drmModeEncoderPtr	drm_enc;
	drmModeCrtcPtr		drm_crtc;
	char			dev[256];
	int			connector_num, mode_num;

	pexit_if(!drmAvailable(),
		"drm unavailable");

	ask_string(dev, sizeof(dev), "DRM device", "/dev/dri/card0");

	pexit_if((drm_fd = open(dev, O_RDWR)) < 0,
		"unable to open drm device \"%s\"", dev);

	pexit_if(!(drm_ver = drmGetVersion(drm_fd)),
		"unable to get drm version");

	printf("\nVersion: %i.%i.%i\nName: \"%.*s\"\nDate: \"%.*s\"\nDescription: \"%.*s\"\n\n",
		drm_ver->version_major,
		drm_ver->version_minor,
		drm_ver->version_patchlevel,
		drm_ver->name_len,
		drm_ver->name,
		drm_ver->date_len,
		drm_ver->date,
		drm_ver->desc_len,
		drm_ver->desc);

	pexit_if(!(drm_res = drmModeGetResources(drm_fd)),
		"unable to get drm resources");

	printf("\nConnectors\n");
	connected = 0;
	for (i = 0; i < drm_res->count_connectors; i++) {

		pexit_if(!(drm_con = drmModeGetConnector(drm_fd, drm_res->connectors[i])),
			"unable to get connector %x", (int)drm_res->connectors[i]);

		if (!drm_con->encoder_id) {
			continue;
		}

		pexit_if(!(drm_enc = drmModeGetEncoder(drm_fd, drm_con->encoder_id)),
			"unable to get encoder %x", (int)drm_con->encoder_id);

		connected++;

		printf(" %i: %s (%s%s%s)\n",
			i, connector_type_name(drm_con->connector_type),
			connection_type_name(drm_con->connection),
			drm_con->encoder_id ? " via " : "",
			drm_con->encoder_id ? encoder_type_name(drm_enc->encoder_type) : "");
			/* TODO show mmWidth/mmHeight? */
	}

	exit_if(!connected,
		"No connectors available, try different card or my bug?");
	ask_num(&connector_num, drm_res->count_connectors, "Select connector", 0); // TODO default? 

	pexit_if(!(drm_con = drmModeGetConnector(drm_fd, drm_res->connectors[connector_num])),
		"unable to get connector %x", (int)drm_res->connectors[connector_num]);
	pexit_if(!(drm_enc = drmModeGetEncoder(drm_fd, drm_con->encoder_id)),
		"unable to get encoder %x", (int)drm_con->encoder_id);

	pexit_if(!(drm_crtc = drmModeGetCrtc(drm_fd, drm_enc->crtc_id)),
		"unable to get crtc %x", (int)drm_enc->crtc_id);

	*res_drm_fd = drm_fd;
	*res_crtc_id = drm_crtc->crtc_id;
	*res_connector_id = drm_con->connector_id;

	printf("\nModes\n");
	for (i = 0; i < drm_con->count_modes; i++) {
		printf(" %i: %s @ %"PRIu32"Hz\n",
			i, drm_con->modes[i].name,
			drm_con->modes[i].vrefresh);
	}
	ask_num(&mode_num, drm_con->count_modes, "Select mode", 0); // TODO default to &drm_crtc->mode?

	*res_mode = &drm_con->modes[mode_num];
}
