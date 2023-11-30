#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "til_fb.h"
#include "til_settings.h"
#include "til_util.h"
#include "til_video_setup.h"

/* drm fb backend, everything drm-specific in rototiller resides here. */
typedef struct drm_fb_page_t drm_fb_page_t;

struct drm_fb_page_t {
	drm_fb_page_t		*next_spare;
	uint32_t		*mmap;
	uint32_t		*shadow;
	size_t			mmap_size, pitch;
	uint32_t		drm_dumb_handle;
	uint32_t		drm_fb_id;
};

typedef struct drm_fb_t {
	int			drm_fd;
	drmModeCrtc		*crtc;
	drmModeConnector	*connector;
	drmModeModeInfo		*mode;
	drm_fb_page_t		*spare_pages;
	unsigned		use_shadow:1;
} drm_fb_t;

typedef struct drm_fb_setup_t {
	til_video_setup_t	til_video_setup;
	const char		*dev;
	const char		*connector;
	const char		*mode;
} drm_fb_setup_t;


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


static int dev_desc_generator(const til_settings_t *settings, til_setup_t *setup_context, const til_setting_desc_t **res_desc)
{
	return  til_setting_desc_new(	settings,
					&(til_setting_spec_t){
						.name = "DRM device path",
						.key = "dev",
						.regex = "/dev/dri/card[0-9]",
						.preferred = "/dev/dri/card0",
						.values = NULL,
						.annotations = NULL
					}, res_desc);
}


/* returns a NULL-terminated array of drm connectors */
static int get_connectors(const char *dev, char ***res_connectors)
{
	int		counts[64] = {};	/* assuming this is big enough */
	char		**connectors;
	drmModeRes	*res;
	int		fd;

	assert(dev);

	fd = open(dev, O_RDWR);
	if (fd == -1)
		return -errno;

	res = drmModeGetResources(fd);
	if (!res) {
		close(fd);

		return -errno;
	}

	connectors = calloc(res->count_connectors + 1, sizeof(*connectors));
	if (!connectors) {
		close(fd);

		return -ENOMEM;
	}

	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector	*con;

		con = drmModeGetConnector(fd, res->connectors[i]);
		if (!con) {
			close(fd);

			return -errno;
		}

		counts[con->connector_type]++;
		asprintf(&connectors[i], "%s-%i", connector_type_name(con->connector_type), counts[con->connector_type]); /* TODO: errors */

		drmModeFreeConnector(con);
	}

	drmModeFreeResources(res);
	close(fd);

	*res_connectors = connectors;

	return 0;
}


static void free_strv(const char **strv)
{
	int	i;

	for (i = 0; strv[i]; i++)
		free((void *)strv[i]);

	free((void *)strv);
}


static int connector_desc_generator(const til_settings_t *settings, til_setup_t *setup_context, const til_setting_desc_t **res_desc)
{
	drm_fb_setup_t	*s = (drm_fb_setup_t *)setup_context;
	const char	**connectors = NULL;
	int		r;

	assert(s);

	r = get_connectors(s->dev, (char ***)&connectors);
	if (r < 0)
		return r;

	if (!connectors[0])
		return -ENOENT;

	r = til_setting_desc_new(	settings,
					&(til_setting_spec_t){
						.name = "DRM connector",
						.key = "connector",
						.regex = "[a-zA-Z0-9]+",
						.preferred = connectors[0],
						.values = connectors,
						.annotations = NULL
					}, res_desc);
	free_strv(connectors);

	return r;
}


static int lookup_connector(int fd, const char *connector, drmModeConnector **res_connector)
{
	int			r = -ENOENT, counts[64] = {};	/* assuming this is big enough */
	drmModeConnector	*con = NULL;
	drmModeRes		*res;

	res = drmModeGetResources(fd);
	if (!res)
		return -errno;

	for (int i = 0; i < res->count_connectors; i++) {
		char	*str;

		con = drmModeGetConnector(fd, res->connectors[i]);
		if (!con) {
			r = -errno;
			goto _out_res;
		}

		counts[con->connector_type]++;
		asprintf(&str, "%s-%i", connector_type_name(con->connector_type), counts[con->connector_type]); /* TODO: errors */

		if (!strcasecmp(str, connector)) {
			free(str);

			break;
		}

		free(str);
		drmModeFreeConnector(con);
		con = NULL;
	}

_out_res:
	drmModeFreeResources(res);

	if (!con)
		return r;

	*res_connector = con;

	return 0;
}


/* returns a NULL-terminated array of drm modes for the supplied device and connector */
static int get_modes(const char *dev, const char *connector, const char ***res_modes)
{
	char			**modes = NULL;
	int			fd, r = 0;
	drmModeConnector	*con;

	assert(dev);
	assert(connector);

	fd = open(dev, O_RDWR);
	if (fd == -1)
		return -errno;

	r = lookup_connector(fd, connector, &con);
	if (r < 0)
		goto _out_fd;

	modes = calloc(con->count_modes + 1, sizeof(*modes));
	if (!modes) {
		r = -ENOMEM;
		goto _out_con;
	}

	for (int i = 0; i < con->count_modes; i++)
		asprintf(&modes[i], "%s@%"PRIu32, con->modes[i].name, con->modes[i].vrefresh);

	*res_modes = (const char **)modes;

_out_con:
	drmModeFreeConnector(con);
_out_fd:
	close(fd);

	return r;
}


static int mode_desc_generator(const til_settings_t *settings, til_setup_t *setup_context, const til_setting_desc_t **res_desc)
{
	drm_fb_setup_t	*s = (drm_fb_setup_t *)setup_context;
	const char	**modes = NULL;
	int		r;

	assert(s);

	r = get_modes(s->dev, s->connector, &modes);
	if (r < 0)
		return r;

	if (!modes[0])
		return -ENOENT;

	r = til_setting_desc_new(	settings,
					&(til_setting_spec_t){
						.name = "DRM video mode",
						.key = "mode",
						.regex = "[0-9]+[xX][0-9]+@[0-9]+",
						.preferred = modes[0],
						.values = modes,
						.annotations = NULL
					}, res_desc);
	free_strv(modes);

	return r;
}


static void drm_fb_setup_free(til_setup_t *setup)
{
	drm_fb_setup_t	*s = (drm_fb_setup_t *)setup;

	assert(s);

	free((void *)s->dev);
	free((void *)s->connector);
	free((void *)s->mode);
}


/* lookup a mode string in the given connector returning its respective modeinfo */
static drmModeModeInfo * lookup_mode(drmModeConnector *connector, const char *mode)
{
	int	i;

	assert(connector);
	assert(mode);

	for (i = 0; i < connector->count_modes; i++) {
		char	*str;

		asprintf(&str, "%s@%"PRIu32, connector->modes[i].name, connector->modes[i].vrefresh);
		if (!strcasecmp(str, mode)) {
			free(str);

			return &connector->modes[i];
		}

		free(str);
	}

	return NULL;
}


/* prepare the drm context for use with the supplied settings */
static int drm_fb_init(const char *title, const til_video_setup_t *setup, void **res_context)
{
	drm_fb_setup_t	*s = (drm_fb_setup_t *)setup;
	drm_fb_t	*c;
	drmModeEncoder	*enc;
	int		r;

	assert(setup);

	if (!drmAvailable()) {
		r = -errno;
		goto _err;
	}

	if (!s->dev || !s->connector || !s->mode) {
		r = -EINVAL;
		goto _err;
	}

	c = calloc(1, sizeof(drm_fb_t));
	if (!c) {
		r = -ENOMEM;
		goto _err;
	}

	c->drm_fd = open(s->dev, O_RDWR);
	if (c->drm_fd < 0) {
		r = -errno;
		goto _err_ctxt;
	}

	r = lookup_connector(c->drm_fd, s->connector, &c->connector);
	if (r < 0)
		goto _err_fd;

	c->mode = lookup_mode(c->connector, s->mode);
	if (!c->mode) {
		r = -EINVAL;
		goto _err_con;
	}

	enc = drmModeGetEncoder(c->drm_fd, c->connector->encoder_id);
	if (!enc) {
		r = -errno;
		goto _err_con;
	}

	c->crtc = drmModeGetCrtc(c->drm_fd, enc->crtc_id);
	if (!c->crtc) {
		r = -errno;
		goto _err_enc;
	}

	{
		uint64_t	use_shadow;

		if (!drmGetCap(c->drm_fd, DRM_CAP_DUMB_PREFER_SHADOW, &use_shadow) && use_shadow)
			c->use_shadow = 1;
	}

	drmModeFreeEncoder(enc);

	*res_context = c;

	return 0;

_err_enc:
	drmModeFreeEncoder(enc);
_err_con:
	drmModeFreeConnector(c->connector);
_err_fd:
	close(c->drm_fd);
_err_ctxt:
	free(c);
_err:
	return r;
}


static void _drm_fb_page_free(drm_fb_t *fb, drm_fb_page_t *page)
{
	struct drm_mode_destroy_dumb	destroy_dumb = {};

	drmModeRmFB(fb->drm_fd, page->drm_fb_id);
	munmap(page->mmap, page->mmap_size);

	destroy_dumb.handle = page->drm_dumb_handle;
	ioctl(fb->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb); // XXX: errors?

	free(page->shadow);
	free(page);
}


static void drm_fb_shutdown(til_fb_t *fb, void *context)
{
	drm_fb_t	*c = context;
	drm_fb_page_t	*p;

	assert(c);

	while ((p = c->spare_pages)) {
		c->spare_pages = p->next_spare;

		_drm_fb_page_free(c, p);
	}

	close(c->drm_fd);
	drmModeFreeConnector(c->connector);
	drmModeFreeCrtc(c->crtc);
	free(c);
}


static int drm_fb_acquire(til_fb_t *fb, void *context, void *page)
{
	drm_fb_t	*c = context;
	drm_fb_page_t	*p = page;

	return drmModeSetCrtc(c->drm_fd, c->crtc->crtc_id, p->drm_fb_id, 0, 0, &c->connector->connector_id, 1, c->mode);
}


static void drm_fb_release(til_fb_t *fb, void *context)
{
	/* TODO restore the existing mode @ last acquire? */
}


static void * drm_fb_page_alloc(til_fb_t *fb, void *context, til_fb_fragment_t *res_fragment)
{
	struct drm_mode_create_dumb	create_dumb = { .bpp = 32 };
	struct drm_mode_map_dumb	map_dumb = {};
	uint32_t			*map, fb_id;
	drm_fb_t			*c = context;
	drm_fb_page_t			*p = NULL;

	if (c->spare_pages) {
		p = c->spare_pages;
		c->spare_pages = p->next_spare;
	}

	if (!p) {
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

		/* prevent unaligned pitches, we're just simplifying everything in rototiller that wants
		 * to do word-at-a-time operations without concern for arches that get angry when that happens
		 * on unaligned addresses.
		 */
		assert(!(create_dumb.pitch & 0x3));

		if (c->use_shadow) {
			pexit_if(!(p->shadow = malloc(create_dumb.size)),
				"unable to allocate page shadow buffer");
		}

		p->mmap = map;
		p->mmap_size = create_dumb.size;
		p->pitch = create_dumb.pitch >> 2;
		p->drm_dumb_handle = map_dumb.handle;
		p->drm_fb_id = fb_id;
	}

	*res_fragment =	(til_fb_fragment_t){
				.buf = p->shadow ? : p->mmap,
				.width = c->mode->hdisplay,
				.frame_width = c->mode->hdisplay,
				.height = c->mode->vdisplay,
				.frame_height = c->mode->vdisplay,
				.pitch = p->pitch,
				.stride = p->pitch - c->mode->hdisplay,
			};

	return p;
}


static int drm_fb_page_free(til_fb_t *fb, void *context, void *page)
{
	drm_fb_t	*c = context;
	drm_fb_page_t	*p = page;

	p->next_spare = c->spare_pages;
	c->spare_pages = p;

	return 0;
}


static int drm_fb_page_flip(til_fb_t *fb, void *context, void *page)
{
	drmEventContext	drm_ev_ctx = {
				.version = DRM_EVENT_CONTEXT_VERSION,
				.vblank_handler = NULL,
				.page_flip_handler = NULL
			};
	drm_fb_t	*c = context;
	drm_fb_page_t	*p = page;

	if (p->shadow)
		memcpy(p->mmap, p->shadow, p->mmap_size);

	if (drmModePageFlip(c->drm_fd, c->crtc->crtc_id, p->drm_fb_id, DRM_MODE_PAGE_FLIP_EVENT, NULL) < 0)
		return -1;

	return drmHandleEvent(c->drm_fd, &drm_ev_ctx);
}


static int drm_fb_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_fb_ops_t drm_fb_ops = {
	.setup = drm_fb_setup,
	.init = drm_fb_init,
	.shutdown = drm_fb_shutdown,
	.acquire = drm_fb_acquire,
	.release = drm_fb_release,
	.page_alloc = drm_fb_page_alloc,
	.page_free = drm_fb_page_free,
	.page_flip = drm_fb_page_flip
};


/* setup is called repeatedly as settings is constructed, until 0 is returned. */
/* a negative value is returned on error */
/* positive value indicates another setting is needed, described in next_setting */
static int drm_fb_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	drm_fb_setup_t			*setup = til_setup_new(settings, sizeof(*setup), drm_fb_setup_free, &drm_fb_ops);
	til_setting_desc_generator_t	generators[] = {
						{
							.key = "dev",
							.value_ptr = &setup->dev,
							.func = dev_desc_generator
						}, {
							.key = "connector",
							.value_ptr = &setup->connector,
							.func = connector_desc_generator
						}, {
							.key = "mode",
							.value_ptr = &setup->mode,
							.func = mode_desc_generator
						},
					};

	if (!drmAvailable())
		return -ENOSYS;

	if (!setup)
		return -ENOMEM;

	return til_settings_apply_desc_generators(settings, generators, nelems(generators), &setup->til_video_setup.til_setup, res_setting, res_desc, res_setup);
}


