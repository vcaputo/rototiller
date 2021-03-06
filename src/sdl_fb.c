#define SDL_MAIN_HANDLED
#include <assert.h>
#include <SDL.h>
#include <stdlib.h>
#include <errno.h>

#include "fb.h"
#include "settings.h"


/* sdl fb backend, everything sdl-specific in rototiller resides here. */

typedef struct sdl_fb_t {
	unsigned	width, height;
	Uint32		flags;

	SDL_Window	*window;
	SDL_Renderer	*renderer;
	SDL_Texture	*texture;
} sdl_fb_t;

typedef struct sdl_fb_page_t sdl_fb_page_t;

struct sdl_fb_page_t {
	SDL_Surface	*surface;
};


static int sdl_fb_setup(const settings_t *settings, setting_desc_t **next_setting)
{
	const char		*fullscreen_values[] = {
					"off",
					"on",
					NULL
				};
	const setting_desc_t	descs[] = {
					{
						.name = "SDL Fullscreen Mode",
						.key = "fullscreen",
						.regex = NULL,
						.preferred = fullscreen_values[0],
						.values = fullscreen_values,
						.annotations = NULL
					}, {
						.name = "SDL Window size",
						.key = "size",
						.regex = "[1-9][0-9]*[xX][1-9][0-9]*",
						.preferred = "640x480",
						.values = NULL,
						.annotations = NULL
					},
				};
	const char		*fullscreen;
	int			r;


	fullscreen = settings_get_value(settings, "fullscreen");
	if (!fullscreen) {
		r = setting_desc_clone(&descs[0], next_setting);
		if (r < 0)
			return r;

		return 1;
	}

	r = setting_desc_check(&descs[0], fullscreen);
	if (r < 0)
		return r;

	if (!strcasecmp(fullscreen, "off")) {
		const char	*size;

		size = settings_get_value(settings, "size");
		if (!size) {
			r = setting_desc_clone(&descs[1], next_setting);
			if (r < 0)
				return r;

			return 1;
		}

		r = setting_desc_check(&descs[1], size);
		if (r < 0)
			return r;
	}

	return 0;
}

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

static int sdl_fb_init(const settings_t *settings, void **res_context)
{
	const char	*fullscreen;
	const char	*size;
	sdl_fb_t	*c;
	int		r;

	assert(settings);
	assert(res_context);

	fullscreen = settings_get_value(settings, "fullscreen");
	if (!fullscreen)
		return -EINVAL;

	size = settings_get_value(settings, "size");
	if (!size && !strcasecmp(fullscreen, "off"))
		return -EINVAL;

	c = calloc(1, sizeof(sdl_fb_t));
	if (!c)
		return -ENOMEM;

	if (!strcasecmp(fullscreen, "on")) {
		if (!size)
			c->flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
		else
			c->flags = SDL_WINDOW_FULLSCREEN;
	}

	if (size) /* TODO: errors */
		sscanf(size, "%u%*[xX]%u", &c->width, &c->height);

	SDL_SetMainReady();
	r = SDL_Init(SDL_INIT_VIDEO);
	if (r < 0) {
		free(c);
		return -sdl_err_to_errno(r);
	}

	if (c->flags == SDL_WINDOW_FULLSCREEN_DESKTOP) {
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


static void sdl_fb_shutdown(fb_t *fb, void *context)
{
	sdl_fb_t	*c = context;

	SDL_Quit();
	free(c);
}


static int sdl_fb_acquire(fb_t *fb, void *context, void *page)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p = page;

	c->window = SDL_CreateWindow("rototiller", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, c->width, c->height, c->flags);
	if (!c->window)
		return -1;

	c->renderer = SDL_CreateRenderer(c->window, -1, SDL_RENDERER_PRESENTVSYNC);
	if (!c->renderer)
		return -1;

	c->texture = SDL_CreateTexture(c->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, c->width, c->height);
	if (!c->texture)
		return -1;

	return 0;
}


static void sdl_fb_release(fb_t *fb, void *context)
{
	sdl_fb_t	*c = context;

	SDL_DestroyTexture(c->texture);
	SDL_DestroyRenderer(c->renderer);
	SDL_DestroyWindow(c->window);
}


static void * sdl_fb_page_alloc(fb_t *fb, void *context, fb_page_t *res_page)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p;

	p = calloc(1, sizeof(sdl_fb_page_t));
	if (!p)
		return NULL;

	p->surface = SDL_CreateRGBSurface(0, c->width, c->height, 32, 0, 0, 0, 0);

	res_page->fragment.buf = p->surface->pixels;
	res_page->fragment.width = c->width;
	res_page->fragment.frame_width = c->width;
	res_page->fragment.height = c->height;
	res_page->fragment.frame_height = c->height;
	res_page->fragment.stride = p->surface->pitch - (c->width * 4);
	res_page->fragment.pitch = p->surface->pitch;

	return p;
}


static int sdl_fb_page_free(fb_t *fb, void *context, void *page)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p = page;

	SDL_FreeSurface(p->surface);
	free(p);

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
	}

	return 0;
}


static int sdl_fb_page_flip(fb_t *fb, void *context, void *page)
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


fb_ops_t sdl_fb_ops = {
	.setup = sdl_fb_setup,
	.init = sdl_fb_init,
	.shutdown = sdl_fb_shutdown,
	.acquire = sdl_fb_acquire,
	.release = sdl_fb_release,
	.page_alloc = sdl_fb_page_alloc,
	.page_free = sdl_fb_page_free,
	.page_flip = sdl_fb_page_flip
};
