#ifndef _SDL_FB_H
#define _SDL_FB_H

typedef struct sdl_fb_t sdl_fb_t;

sdl_fb_t * sdl_fb_new();
void sdl_fb_free(sdl_fb_t *fb);

#endif
