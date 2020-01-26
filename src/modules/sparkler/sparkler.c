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
	unsigned	n_cpus;
} sparkler_context_t;

extern particle_ops_t	simple_ops;


static void * sparkler_create_context(unsigned ticks, unsigned num_cpus)
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


static int sparkler_fragmenter(void *context, const fb_fragment_t *fragment, unsigned number, fb_fragment_t *res_fragment)
{
	sparkler_context_t	*ctxt = context;

	return fb_fragment_slice_single(fragment, ctxt->n_cpus, number, res_fragment);
}

static void sparkler_prepare_frame(void *context, unsigned ticks, unsigned ncpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter)
{
	sparkler_context_t	*ctxt = context;

	*res_fragmenter = sparkler_fragmenter;
	ctxt->n_cpus = ncpus;

	particles_sim(ctxt->particles);
	particles_add_particles(ctxt->particles, NULL, &simple_ops, INIT_PARTS / 4);
	particles_age(ctxt->particles);
}


/* Render a 3D particle system */
static void sparkler_render_fragment(void *context, unsigned ticks, unsigned cpu, fb_fragment_t *fragment)
{
	sparkler_context_t	*ctxt = context;

	fb_fragment_zero(fragment);
	particles_draw(ctxt->particles, fragment);
}


rototiller_module_t	sparkler_module = {
	.create_context = sparkler_create_context,
	.destroy_context = sparkler_destroy_context,
	.prepare_frame = sparkler_prepare_frame,
	.render_fragment = sparkler_render_fragment,
	.name = "sparkler",
	.description = "Particle system with spatial interactions (threaded (poorly))",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
