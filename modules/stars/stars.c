#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "draw.h"
#include "fb.h"
#include "rototiller.h"
#include "starslib.h"

/* Copyright (C) 2017 Philip J. Freeman <elektron@halo.nu> */

static void stars(fb_fragment_t *fragment)
{
	static int	initialized, z;
	static struct	universe* u;

	struct		return_point rp;
	int		x, y, width = fragment->width, height = fragment->height;

	if (!initialized) {
		z = 128;
		srand(time(NULL) + getpid());

		// Initialize the stars lib (and pre-add a bunch of stars)
		new_universe(&u, width, height, z);
		for(y=0; y<z; y++) {
			while (process_point(u, &rp) != 0);
			for(x=0; x<rand()%128; x++){
				new_point(u);
			}
		}
		initialized = 1;
	}

	// draw space (or blank the frame, if you prefer)
	memset(fragment->buf, 0, ((fragment->width << 2) + fragment->stride) * fragment->height);

	// draw stars
	for (;;) {
		int ret = process_point( u, &rp  );
		if (ret==0) break;
		if (ret==1) draw_pixel(fragment, rp.x+(width/2), rp.y+(height/2),
				       makergb(0xFF, 0xFF, 0xFF, (float)rp.opacity/OPACITY_MAX)
				      );
	}

	// add stars at horizon
	for (x=0; x<rand()%128; x++) {
		new_point(u);
	}
}

rototiller_renderer_t	stars_renderer = {
	.render = stars,
	.name = "stars",
	.description = "basic starfield",
	.author = "Philip J Freeman <elektron@halo.nu>",
	.license = "GPLv2",
};
