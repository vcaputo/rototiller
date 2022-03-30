#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "til.h"
#include "til_fb.h"
#include "til_util.h"

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

static void * sparkler_create_context(unsigned ticks, unsigned num_cpus, void *setup)
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


static int sparkler_fragmenter(void *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	sparkler_context_t	*ctxt = context;

	return til_fb_fragment_slice_single(fragment, ctxt->n_cpus, number, res_fragment);
}

static void sparkler_prepare_frame(void *context, unsigned ticks, unsigned ncpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter)
{
	sparkler_context_t	*ctxt = context;

	*res_fragmenter = sparkler_fragmenter;
	ctxt->n_cpus = ncpus;

	if (sparkler_conf.show_bsp_matches)
		til_fb_fragment_zero(fragment);

	particles_sim(ctxt->particles, fragment);
	particles_add_particles(ctxt->particles, NULL, &simple_ops, INIT_PARTS / 4);
	particles_age(ctxt->particles);
}


/* Render a 3D particle system */
static void sparkler_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	sparkler_context_t	*ctxt = context;

	if (!sparkler_conf.show_bsp_matches)
		til_fb_fragment_zero(fragment);

	particles_draw(ctxt->particles, fragment);
}


/* Settings hooks for configurable variables */
static int sparkler_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, void **res_setup)
{
	const char	*show_bsp_leafs;
	const char	*show_bsp_matches;
	const char	*values[] = {
			       "off",
			       "on",
			       NULL
			};
	int		r;

	/* TODO: return -EINVAL on parse errors? */

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Show BSP-tree leaf-node bounding boxes",
							.key = "show_bsp_leafs",
							.preferred = "off",
							.values = values
						},
						&show_bsp_leafs,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (!strcasecmp(show_bsp_leafs, "on")) {
		const char	*depth_values[] = {
					"0",
					"4",
					"6",
					"8",
					"10",
					NULL
				};
		const char	*show_bsp_leafs_min_depth;

		sparkler_conf.show_bsp_leafs = 1;

		r = til_settings_get_and_describe_value(settings,
							&(til_setting_desc_t){
								.name = "Minimum BSP-tree depth for shown leaf-nodes",
								.key = "show_bsp_leafs_min_depth",
								.preferred = "8",
								.values = depth_values
							},
							&show_bsp_leafs_min_depth,
							res_setting,
							res_desc);
		if (r)
			return r;

		sscanf(show_bsp_leafs_min_depth, "%u", &sparkler_conf.show_bsp_leafs_min_depth);
	} else {
		sparkler_conf.show_bsp_leafs = 0;
	}

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
							.name = "Show BSP-tree search broad-phase match candidates",
							.key = "show_bsp_matches",
							.preferred = "off",
							.values = values
						},
						&show_bsp_matches,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (!strcasecmp(show_bsp_matches, "on"))
		sparkler_conf.show_bsp_matches = 1;
	else
		sparkler_conf.show_bsp_matches = 0;

	if (!strcasecmp(show_bsp_matches, "on")) {
		const char	*show_bsp_matches_affected_only;

		r = til_settings_get_and_describe_value(settings,
							&(til_setting_desc_t){
								.name = "Show only narrow-phase affected match results",
								.key = "show_bsp_matches_affected_only",
								.preferred = "off",
								.values = values
							},
							&show_bsp_matches_affected_only,
							res_setting,
							res_desc);
		if (r)
			return r;

		if (!strcasecmp(show_bsp_matches_affected_only, "on"))
			sparkler_conf.show_bsp_matches_affected_only = 1;
		else
			sparkler_conf.show_bsp_matches_affected_only = 0;
	}

	return 0;
}


til_module_t	sparkler_module = {
	.create_context = sparkler_create_context,
	.destroy_context = sparkler_destroy_context,
	.prepare_frame = sparkler_prepare_frame,
	.render_fragment = sparkler_render_fragment,
	.setup = sparkler_setup,
	.name = "sparkler",
	.description = "Particle system with spatial interactions (threaded (poorly))",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
