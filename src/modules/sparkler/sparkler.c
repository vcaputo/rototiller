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

typedef struct sparkler_context_t {
	particles_t	*particles;
} sparkler_context_t;

extern particle_ops_t	simple_ops;


static void * sparkler_create_context(void)
{
	static int		initialized;
	sparkler_context_t	*ctxt;

	if (!initialized) {
		srand(time(NULL) + getpid());
		initialized = 1;
	}

	ctxt = calloc(1, sizeof(sparkler_context_t));
	if (!ctxt)
		return NULL;

	ctxt->particles = particles_new();
	if (!ctxt->particles) {
		free(ctxt);
		return NULL;
	}

	particles_add_particles(ctxt->particles, NULL, &simple_ops, INIT_PARTS);

	return ctxt;
}


static void sparkler_destroy_context(void *context)
{
	sparkler_context_t	*ctxt = context;

	particles_free(ctxt->particles);
	free(ctxt);
}


/* Render a 3D particle system */
static void sparkler_render_fragment(void *context, fb_fragment_t *fragment)
{
	sparkler_context_t	*ctxt = context;
	uint32_t		*buf = fragment->buf;


	fb_fragment_zero(fragment);

	particles_age(ctxt->particles);
	particles_draw(ctxt->particles, fragment);
	particles_sim(ctxt->particles);
	particles_add_particles(ctxt->particles, NULL, &simple_ops, INIT_PARTS / 4);
}


rototiller_module_t	sparkler_module = {
	.create_context = sparkler_create_context,
	.destroy_context = sparkler_destroy_context,
	.render_fragment = sparkler_render_fragment,
	.name = "sparkler",
	.description = "Particle system with spatial interactions",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
