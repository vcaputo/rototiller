#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "fb.h"
#include "rototiller.h"
#include "util.h"

#include "particles.h"

/* particle system gadget (C) Vito Caputo <vcaputo@pengaru.com> 2/15/2014 */
/* 1/10/2015 added octree bsp (though not yet leveraged) */
/* 11/25/2016 refactor and begun adapting to rototiller */

#define INIT_PARTS 100

extern particle_ops_t	simple_ops;


/* Render a 3D particle system */
static void sparkler(fb_fragment_t *fragment)
{
	static particles_t	*particles;
	static int		initialized;
	uint32_t		*buf = fragment->buf;

	if (!initialized) {
		srand(time(NULL) + getpid());

		particles = particles_new();
		particles_add_particles(particles, NULL, &simple_ops, INIT_PARTS);

		initialized = 1;
	}

	particles_age(particles);
	memset(buf, 0, ((fragment->width << 2) + fragment->stride) * fragment->height);

	particles_draw(particles, fragment);
	particles_sim(particles);
	particles_add_particles(particles, NULL, &simple_ops, INIT_PARTS / 4);
}


rototiller_renderer_t	sparkler_renderer = {
	.render = sparkler,
	.name = "sparkler",
	.description = "Particle system with spatial interactions",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
