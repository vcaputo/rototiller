#include <SDL.h>
#include <stdlib.h>
#include <errno.h>

#include "fb.h"
#include "settings.h"


/* sdl fb backend, everything sdl-specific in rototiller resides here. */

typedef struct sdl_fb_t {
	SDL_Window	*window;
	SDL_Renderer	*renderer;
	SDL_Texture	*texture;
} sdl_fb_t;

typedef struct sdl_fb_page_t sdl_fb_page_t;

struct sdl_fb_page_t {
	SDL_Surface	*surface;
};


int sdl_fb_setup(const settings_t *settings, setting_desc_t **next_setting)
{
	/* TODO: window size? fullscreen? vsync? etc. */
	return 0;
}


void * sdl_fb_init(const settings_t *settings)
{
	sdl_fb_t	*c;

	c = calloc(1, sizeof(sdl_fb_t));
	if (!c)
		return NULL;

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		free(c);
		return NULL;
	}

	return c;
}


void sdl_fb_shutdown(void *context)
{
	sdl_fb_t	*c = context;

	SDL_Quit();
	free(c);
}


static int sdl_fb_acquire(void *context, void *page)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p = page;

	c->window = SDL_CreateWindow("rototiller", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
	if (!c->window)
		return -1;

	c->renderer = SDL_CreateRenderer(c->window, -1, SDL_RENDERER_PRESENTVSYNC);
	if (!c->renderer)
		return -1;

	c->texture = SDL_CreateTexture(c->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);
	if (!c->texture)
		return -1;

	return 0;
}


static void sdl_fb_release(void *context)
{
	sdl_fb_t	*c = context;

	SDL_DestroyTexture(c->texture);
	SDL_DestroyRenderer(c->renderer);
	SDL_DestroyWindow(c->window);
}


static void * sdl_fb_page_alloc(void *context, fb_page_t *res_page)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p;

	p = calloc(1, sizeof(sdl_fb_page_t));
	if (!p)
		return NULL;


	p->surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_RGB888);

	res_page->fragment.buf = p->surface->pixels;
	res_page->fragment.width = 640;
	res_page->fragment.frame_width = 640;
	res_page->fragment.height = 480;
	res_page->fragment.frame_height = 480;
	res_page->fragment.stride = p->surface->pitch - (640 * 4);

	return p;
}


static int sdl_fb_page_free(void *context, void *page)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p = page;

	SDL_FreeSurface(p->surface);
	free(p);

	return 0;
}


static int sdl_fb_page_flip(void *context, void *page)
{
	sdl_fb_t	*c = context;
	sdl_fb_page_t	*p = page;

	if (SDL_UpdateTexture(c->texture, NULL, p->surface->pixels, p->surface->pitch) < 0)
		return -1;

	if (SDL_RenderClear(c->renderer) < 0)
		return -1;

	if (SDL_RenderCopy(c->renderer, c->texture, NULL, NULL) < 0)
		return -1;

	SDL_RenderPresent(c->renderer);

	if (SDL_QuitRequested())
		return -EPIPE;

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
