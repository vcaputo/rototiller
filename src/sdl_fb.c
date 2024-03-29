#define SDL_MAIN_HANDLED
#include <assert.h>
#include <SDL.h>
#include <stdlib.h>
#include <errno.h>

#include "til_fb.h"
#include "til_settings.h"
#include "til_video_setup.h"


/* sdl fb backend, everything sdl-specific in rototiller resides here. */

typedef struct sdl_fb_setup_t {
	til_video_setup_t	til_video_setup;
	unsigned		fullscreen:1;
	unsigned		vsync:1;
	unsigned		width, height;
} sdl_fb_setup_t;

typedef struct sdl_fb_page_t sdl_fb_page_t;

struct sdl_fb_page_t {
	sdl_fb_page_t	*next_spare;
	SDL_Surface	*surface;
};

typedef struct sdl_fb_t {
	const char	*title;
	unsigned	width, height;
	Uint32		window_flags;
	Uint32		renderer_flags;

	SDL_Window	*window;
	SDL_Renderer	*renderer;
	SDL_Texture	*texture;

	sdl_fb_page_t	*spare_pages;
} sdl_fb_t;


static int sdl_err_to_errno(int err)
{
	switch (err) {
	case SDL_ENOMEM:
		return ENOMEM;
	case SDL_EFREAD:
	case SDL_EFWRITE:
	case SDL_EFSEEK:
		return EIO;
	case SDL_UNSUPPORTED:
		return ENOTSUP;
	default:
		return EINVAL;
	}
}

static int sdl_fb_init(const char *title, const til_video_setup_t *setup, void **res_context)
{
	sdl_fb_setup_t	*s = (sdl_fb_setup_t *)setup;
	sdl_fb_t	*c;
	int		r;

	assert(title);
	assert(setup);
	assert(res_context);

	c = calloc(1, sizeof(sdl_fb_t));
	if (!c)
		return -ENOMEM;

	if (s->fullscreen) {
		if (s->width && s->height)
			c->window_flags = SDL_WINDOW_FULLSCREEN;
		else
			c->window_flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	if (s->vsync)
		c->renderer_flags = SDL_RENDERER_PRESENTVSYNC;

	c->title = strdup(title);
	if (!c->title) {
		free(c);
		return -ENOMEM;
	}

	c->width = s->width;
	c->height = s->height;

	SDL_SetMainReady();
	r = SDL_Init(SDL_INIT_VIDEO);
	if (r < 0) {
		free(c);
		return -sdl_err_to_errno(r);
	}

	if (c->window_flags == SDL_WINDOW_FULLSCREEN_DESKTOP) {
		SDL_DisplayMode	mode;

		r = SDL_GetDesktopDisplayMode(0, &mode);
		if (r != 0) {
			SDL_Quit();
			free(c);
			return -sdl_err_to_errno(r);
		}

		c->width = mode.w;
		c->height = mode.h;
	}

	*res_context = c;

	return 0;
}


static void sdl_fb_shutdown(til_fb_t *fb, void *context)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p;

	while ((p = c->spare_pages)) {
		c->spare_pages = p->next_spare;

		SDL_FreeSurface(p->surface);
		free(p);
	}
	free((void *)c->title);
	SDL_Quit();
	free(c);
}


static int sdl_fb_acquire(til_fb_t *fb, void *context, void *page)
{
	sdl_fb_t	*c = context;

	c->window = SDL_CreateWindow(c->title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, c->width, c->height, c->window_flags);
	if (!c->window)
		return -1;

	c->renderer = SDL_CreateRenderer(c->window, -1, c->renderer_flags);
	if (!c->renderer)
		return -1;

	c->texture = SDL_CreateTexture(c->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, c->width, c->height);
	if (!c->texture)
		return -1;

	if (c->window_flags & SDL_WINDOW_FULLSCREEN || c->window_flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
		SDL_ShowCursor(SDL_DISABLE);

	return 0;
}


static void sdl_fb_release(til_fb_t *fb, void *context)
{
	sdl_fb_t	*c = context;

	SDL_DestroyTexture(c->texture);
	SDL_DestroyRenderer(c->renderer);
	SDL_DestroyWindow(c->window);
}


static void * sdl_fb_page_alloc(til_fb_t *fb, void *context, til_fb_fragment_t *res_fragment)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p = NULL;

	if (c->spare_pages) {
		p = c->spare_pages;
		c->spare_pages = p->next_spare;
	}

	if (!p) {
		p = calloc(1, sizeof(sdl_fb_page_t));
		if (!p)
			return NULL;

		p->surface = SDL_CreateRGBSurface(0, c->width, c->height, 32, 0, 0, 0, 0); /* TODO: errors! */

		/* rototiller wants to assume all pixels to be 32-bit aligned, so prevent unaligning pitches */
		assert(!(p->surface->pitch & 0x3));
	}

	*res_fragment =	(til_fb_fragment_t){
				.buf = p->surface->pixels,
				.width = c->width,
				.frame_width = c->width,
				.height = c->height,
				.frame_height = c->height,
				.pitch = p->surface->pitch >> 2,
				.stride = (p->surface->pitch >> 2) - c->width,
			};

	return p;
}


static int sdl_fb_page_free(til_fb_t *fb, void *context, void *page)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p = page;

	if (p->surface->w != c->width || p->surface->h != c->height) {
		/* TODO FIXME: there seems to be a lack of resizing consideration altogether in this fb backend */
		SDL_FreeSurface(p->surface);
		free(p);
	} else {
		p->next_spare = c->spare_pages;
		c->spare_pages = p;
	}

	return 0;
}


static int sdl_ready()
{
	SDL_Event	ev;

	/* It's important on Windows in particular to
	 * drain the event queue vs. just SDL_QuitRequested()
	 */
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT)
			return -EPIPE;

		if (ev.type == SDL_KEYDOWN &&
		    ev.key.keysym.sym == SDL_GetKeyFromScancode(SDL_SCANCODE_ESCAPE))
			return -EPIPE;
	}

	return 0;
}


static int sdl_fb_page_flip(til_fb_t *fb, void *context, void *page)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p = page;
	int		r;

	r = sdl_ready();
	if (r < 0)
		return r;

	if (SDL_UpdateTexture(c->texture, NULL, p->surface->pixels, p->surface->pitch) < 0)
		return -1;

	if (SDL_RenderClear(c->renderer) < 0)
		return -1;

	if (SDL_RenderCopy(c->renderer, c->texture, NULL, NULL) < 0)
		return -1;

	SDL_RenderPresent(c->renderer);

	return 0;
}


static int sdl_fb_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_fb_ops_t sdl_fb_ops = {
	.setup = sdl_fb_setup,
	.init = sdl_fb_init,
	.shutdown = sdl_fb_shutdown,
	.acquire = sdl_fb_acquire,
	.release = sdl_fb_release,
	.page_alloc = sdl_fb_page_alloc,
	.page_free = sdl_fb_page_free,
	.page_flip = sdl_fb_page_flip
};


static int sdl_fb_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*bool_values[] = {
				"off",
				"on",
				NULL
			};
	til_setting_t	*fullscreen;
	til_setting_t	*vsync;
	til_setting_t	*size;
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "SDL fullscreen mode",
							.key = "fullscreen",
							.regex = NULL,
							.preferred = bool_values[0],
							.values = bool_values,
							.annotations = NULL
						},
						&fullscreen,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "SDL synchronize present with refresh rate",
							.key = "vsync",
							.regex = NULL,
							.preferred = bool_values[1],
							.values = bool_values,
							.annotations = NULL
						},
						&vsync,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (!strcasecmp(fullscreen->value, "off")) {
		r = til_settings_get_and_describe_setting(settings,
							&(til_setting_spec_t){
								.name = "SDL window size",
								.key = "size",
								.regex = "[1-9][0-9]*[xX][1-9][0-9]*",
								.preferred = "640x480",
								.values = NULL,
								.annotations = NULL
							},
							&size,
							res_setting,
							res_desc);
		if (r)
			return r;
	} else if ((size = til_settings_get_setting_by_key(settings, "size", res_setting)) && !size->desc) {
		/* if fullscreen=on AND size=WxH is specified, we'll do a more legacy style SDL fullscreen
		 * where it tries to change the video mode.  But if size is unspecified, it'll be a desktop
		 * style fullscreen where it just uses a fullscreen window in the existing video mode, and
		 * we won't forcibly require a size= be specified.
		 */
		 /* FIXME TODO: this is all copy-n-pasta grossness that wouldn't need to exist if
		  * til_settings_get_and_describe_value() just supported optional settings we only
		  * describe when they're already present.  It just needs something like an optional flag,
		  * to be added in a future commit which will remove this hack.
		  */
		 r = til_setting_desc_new(	settings,
						&(til_setting_spec_t){
							.name = "SDL window size",
							.key = "size",
							.regex = "[1-9][0-9]*[xX][1-9][0-9]*",
							.preferred = "640x480",
							.values = NULL,
							.annotations = NULL
						}, res_desc);
		if (r < 0)
			return r;

		return 1;
	}

	if (res_setup) {
		sdl_fb_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &sdl_fb_ops);
		if (!setup)
			return -ENOMEM;

		if (!strcasecmp(fullscreen->value, "on"))
			setup->fullscreen = 1;

		if (!strcasecmp(vsync->value, "on"))
			setup->vsync = 1;

		if (size && sscanf(size->value, "%u%*[xX]%u", &setup->width, &setup->height) != 2)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_video_setup.til_setup, size, res_setting, -EINVAL);

		*res_setup = &setup->til_video_setup.til_setup;
	}

	return 0;
}
