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

static particles_conf_t	sparkler_conf;

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

	ctxt->particles = particles_new(&sparkler_conf);
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


/* Settings hooks for configurable variables */
static int sparkler_setup(const settings_t *settings, setting_desc_t **next_setting)
{
	const char	*show_bsp_leafs;
	const char	*show_bsp_matches;
	const char	*values[] = {
				"on",
				"off",
				NULL
			};

	show_bsp_leafs = settings_get_value(settings, "show_bsp_leafs");
	if (!show_bsp_leafs) {
		int	r;

		r = setting_desc_clone(&(setting_desc_t){
						.name = "Show BSP Leaf Node Bounding Boxes",
						.key = "show_bsp_leafs",
						.preferred = "off",
						.values = values,
					}, next_setting);
		if (r < 0)
			return r;

		return 1;
	}

	show_bsp_matches = settings_get_value(settings, "show_bsp_matches");
	if (!show_bsp_matches) {
		int	r;

		r = setting_desc_clone(&(setting_desc_t){
						.name = "Show BSP Search Matches",
						.key = "show_bsp_matches",
						.preferred = "off",
						.values = values,
					}, next_setting);
		if (r < 0)
			return r;

		return 1;
	}

	/* TODO: return -EINVAL on parse errors? */
	if (!strcasecmp(show_bsp_leafs, "on"))
		sparkler_conf.show_bsp_leafs = 1;
	else
		sparkler_conf.show_bsp_leafs = 0;

	if (!strcasecmp(show_bsp_matches, "on"))
		sparkler_conf.show_bsp_matches = 1;
	else
		sparkler_conf.show_bsp_matches = 0;

	return 0;
}


rototiller_module_t	sparkler_module = {
	.create_context = sparkler_create_context,
	.destroy_context = sparkler_destroy_context,
	.prepare_frame = sparkler_prepare_frame,
	.render_fragment = sparkler_render_fragment,
	.setup = sparkler_setup,
	.name = "sparkler",
	.description = "Particle system with spatial interactions (threaded (poorly))",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
