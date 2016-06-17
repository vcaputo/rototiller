#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <math.h>

/* Copyright (C) 2016 Vito Caputo <vcaputo@pengaru.com> */

#define exit_if(_cond, _fmt, ...) \
	if (_cond) { \
		fprintf(stderr, "Fatal error: " _fmt "\n", ##__VA_ARGS__); \
		exit(EXIT_FAILURE); \
	}

#define pexit_if(_cond, _fmt, ...) \
	exit_if(_cond, _fmt ": %s", ##__VA_ARGS__, strerror(errno))

/* Some defines for the fixed-point stuff in render(). */
#define FIXED_TRIG_LUT_SIZE	4096	/* size of the cos/sin look-up tables */
#define FIXED_BITS		12	/* fractional bits */
#define FIXED_EXP		4096	/* 2^FIXED_BITS */
#define FIXED_COS(_rad)		costab[_rad % FIXED_TRIG_LUT_SIZE]
#define FIXED_SIN(_rad)		sintab[_rad % FIXED_TRIG_LUT_SIZE]
#define FIXED_MULT(_a, _b)	((_a * _b) >> FIXED_BITS)
#define FIXED_NEW(_i)		(_i << FIXED_BITS)
#define FIXED_TO_INT(_f)	((_f) >> FIXED_BITS)

/* Draw a rotating checkered 256x256 texture into next_page. */
static void render(uint32_t *current_page, uint32_t *next_page, int width, int height, int pitch) {
	static int32_t	costab[FIXED_TRIG_LUT_SIZE], sintab[FIXED_TRIG_LUT_SIZE];
	static uint8_t	texture[256][256];
	static int	initialized;
	static uint32_t	colors[2];
	static unsigned	r, rr;

	int		y_cos_r, y_sin_r, x_cos_r, x_sin_r, x_cos_r_init, x_sin_r_init, cos_r, sin_r;
	int		x, y, stride;
	uint8_t		tx, ty; /* 256x256 texture; 8 bit texture indices to modulo via overflow. */
	uint64_t	*_next_page = (uint64_t *)next_page;

	if (!initialized) {
		int i;

		initialized = 1;

		/* Generate simple checker pattern texture, nothing clever, feel free to play! */
		/* If you modify texture on every frame instead of only @ initialization you can
		 * produce some neat output.  These values are indexed into colors[] below. */
		for (y = 0; y < 128; y++) {
			for (x = 0; x < 128; x++)
				texture[y][x] = 1;
			for (; x < 256; x++)
				texture[y][x] = 0;
		}
		for (; y < 256; y++) {
			for (x = 0; x < 128; x++)
				texture[y][x] = 0;
			for (; x < 256; x++)
				texture[y][x] = 1;
		}

		/* Generate fixed-point cos & sin LUTs. */
		for (i = 0; i < FIXED_TRIG_LUT_SIZE; i++) {
			costab[i] = ((cos((double)2*M_PI*i/FIXED_TRIG_LUT_SIZE))*FIXED_EXP);
			sintab[i] = ((sin((double)2*M_PI*i/FIXED_TRIG_LUT_SIZE))*FIXED_EXP);
		}
	}

	pitch /= 4; /* pitch is number of bytes in a row, scale it to uint32_t units. */
	stride = (pitch - width); /* stride is number of words from row end to start of next row */

	/* This is all done using fixed-point in the hopes of being faster, and yes assumptions
	 * are being made WRT the overflow of tx/ty as well, only tested on x86_64. */
	cos_r = FIXED_COS(r);
	sin_r = FIXED_SIN(r);

	/* Vary the colors, this is just a mashup of sinusoidal rgb values. */
	colors[0] =	((FIXED_TO_INT(FIXED_MULT(FIXED_COS(rr), FIXED_NEW(127))) + 128) << 16) |
			((FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr / 2), FIXED_NEW(127))) + 128) << 8) |
			((FIXED_TO_INT(FIXED_MULT(FIXED_COS(rr / 3), FIXED_NEW(127))) + 128));

	colors[1] =	((FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr / 2), FIXED_NEW(127))) + 128) << 16) |
			((FIXED_TO_INT(FIXED_MULT(FIXED_COS(rr / 2), FIXED_NEW(127))) + 128)) << 8 |
			((FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr), FIXED_NEW(127))) + 128) );

	/* The dimensions are cut in half and negated to center the rotation. */
	/* The [xy]_{sin,cos}_r variables are accumulators to replace multiplication with addition. */
	x_cos_r_init = FIXED_MULT(-FIXED_NEW((width / 2)), cos_r);
	x_sin_r_init = FIXED_MULT(-FIXED_NEW((width / 2)), sin_r);

	y_cos_r = FIXED_MULT(-FIXED_NEW((height / 2)), cos_r);
	y_sin_r = FIXED_MULT(-FIXED_NEW((height / 2)), sin_r);

	width /= 2;
	stride /= 2;

	for (y = 0; y < height; y++) {

		x_cos_r = x_cos_r_init;
		x_sin_r = x_sin_r_init;

		for (x = 0; x < width; x++, _next_page++) {
			uint64_t	p;

			tx = FIXED_TO_INT(x_sin_r - y_cos_r);
			ty = FIXED_TO_INT(y_sin_r + x_cos_r);

			p = colors[texture[ty][tx]];

			x_cos_r += cos_r;
			x_sin_r += sin_r;

			tx = FIXED_TO_INT(x_sin_r - y_cos_r);
			ty = FIXED_TO_INT(y_sin_r + x_cos_r);

			p |= (uint64_t)colors[texture[ty][tx]] << 32;
			
			*_next_page = p;

			x_cos_r += cos_r;
			x_sin_r += sin_r;
		}

		_next_page += stride;
		y_cos_r += cos_r;
		y_sin_r += sin_r;
	}

	// This governs the rotation and color cycle.
	r += FIXED_TO_INT(FIXED_MULT(FIXED_SIN(rr), FIXED_NEW(16)));
	rr += 2;
}



int main(int argc, const char *argv[]) {
	int			drm_fd;
	drmModeResPtr		drm_res;
	drmModeConnectorPtr	drm_con;
	uint32_t		*fb_maps[2], drm_fbs[2];
	unsigned		page = 0, next_page;

	pexit_if(!drmAvailable(),
		"drm unavailable");

	/* FIXME: use drmOpen(), requires digging to see what you're supposed to supply it for name. */
	pexit_if((drm_fd = open("/dev/dri/card0", O_RDWR)) < 0,
		"unable to open drm device");

	/* this requires root, but doesn't seem necessary for what's being done here, which is a bit surprising. */
//	pexit_if(drmSetMaster(drm_fd) < 0,
//		"unable to set master");

	exit_if(!(drm_res = drmModeGetResources(drm_fd)),
		"unable to get drm resources");

	exit_if(drm_res->count_connectors < 1 ||
		!(drm_con = drmModeGetConnector(drm_fd, drm_res->connectors[0])),
		"unable to get first connector");

	/* create double-buffers */
	struct drm_mode_create_dumb create_dumb = {
		.width = drm_con->modes[0].hdisplay,
		.height = drm_con->modes[0].vdisplay,
		.bpp = 32,
		.flags = 0, // unused,
	};
	pexit_if(ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb) < 0,
		"unable to create dumb buffer A");

	struct drm_mode_map_dumb map_dumb = {
		.handle = create_dumb.handle,
		.pad = 0, // unused
	};
	pexit_if(ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb) < 0,
		"unable to prepare dumb buffer A for mmap");
	pexit_if(!(fb_maps[0] = mmap(NULL, create_dumb.size, PROT_READ|PROT_WRITE, MAP_SHARED, drm_fd, map_dumb.offset)),
		"unable to mmap dumb buffer A");

	pexit_if(drmModeAddFB(drm_fd, create_dumb.width, create_dumb.height, 24, create_dumb.bpp, create_dumb.pitch, create_dumb.handle, &drm_fbs[0]) < 0,
		"unable to add dumb buffer A as fb");

	/* second one... */
	pexit_if(ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb) < 0,
		"unable to create dumb buffer B");
	pexit_if(drmModeAddFB(drm_fd, create_dumb.width, create_dumb.height, 24, create_dumb.bpp, create_dumb.pitch, create_dumb.handle, &drm_fbs[1]) < 0,
		"unable to add dumb buffer B as fb");

	map_dumb.handle = create_dumb.handle;

	pexit_if(ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb) < 0,
		"unable to prepare dumb buffer B for mmap");
	pexit_if(!(fb_maps[1] = mmap(NULL, create_dumb.size, PROT_READ|PROT_WRITE, MAP_SHARED, drm_fd, map_dumb.offset)),
		"unable to mmap dumb buffer B");

	/* make the current page the visible one */
	pexit_if(drmModeSetCrtc(drm_fd, drm_res->crtcs[0], drm_fbs[page], 0, 0, drm_res->connectors, 1, drm_con->modes) < 0,
		"unable to configure crtc");

	drmEventContext drm_ev_ctx = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.vblank_handler = NULL,
		.page_flip_handler = NULL
	};

	// now the rendering & page-flipping loop */
	for (;;page = next_page) {
		next_page = (page + 1) % 2;

		/* render next page */
		render(fb_maps[page], fb_maps[next_page], create_dumb.width, create_dumb.height, create_dumb.pitch);

		/* flip synchronously */
		pexit_if(drmModePageFlip(drm_fd, drm_res->crtcs[0], drm_fbs[next_page], DRM_MODE_PAGE_FLIP_EVENT, NULL) < 0,
			"unable to flip page %u to %u", page, next_page);
		drmHandleEvent(drm_fd, &drm_ev_ctx);
	}

	return EXIT_SUCCESS;
}
