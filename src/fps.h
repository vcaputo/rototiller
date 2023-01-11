#ifndef _FPS_H
#define _FPS_H

#include <stdio.h>

#include "til_fb.h"

int fps_setup(void);
void fps_fprint(til_fb_t *fb, FILE *out);

#endif
