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

typedef struct sparkler_setup_t {
	til_setup_t		til_setup;
	unsigned		show_bsp_leafs:1;
	unsigned		show_bsp_matches:1;
	unsigned		show_bsp_matches_affected_only:1;
	unsigned		show_bsp_leafs_min_depth;
} sparkler_setup_t;

typedef struct sparkler_context_t {
	particles_t		*particles;
	unsigned		n_cpus;
	sparkler_setup_t	setup;
} sparkler_context_t;

extern particle_ops_t	simple_ops;

static sparkler_setup_t sparkler_default_setup;

static void * sparkler_create_context(unsigned ticks, unsigned num_cpus, til_setup_t *setup)
{
	sparkler_context_t	*ctxt;

	if (!setup)
		setup = &sparkler_default_setup.til_setup;

	ctxt = calloc(1, sizeof(sparkler_context_t));
	if (!ctxt)
		return NULL;

	ctxt->setup = *(sparkler_setup_t *)setup;

	ctxt->particles = particles_new(&(particles_conf_t){
						.show_bsp_leafs = ((sparkler_setup_t *)setup)->show_bsp_leafs,
						.show_bsp_matches = ((sparkler_setup_t *)setup)->show_bsp_matches,
						.show_bsp_leafs_min_depth = ((sparkler_setup_t *)setup)->show_bsp_leafs_min_depth,
						.show_bsp_matches_affected_only = ((sparkler_setup_t *)setup)->show_bsp_matches_affected_only,
					});
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

	if (ctxt->setup.show_bsp_matches)
		til_fb_fragment_clear(fragment);

	particles_sim(ctxt->particles, fragment);
	particles_add_particles(ctxt->particles, NULL, &simple_ops, INIT_PARTS / 4);
	particles_age(ctxt->particles);
}


/* Render a 3D particle system */
static void sparkler_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	sparkler_context_t	*ctxt = context;

	if (!ctxt->setup.show_bsp_matches)
		til_fb_fragment_clear(fragment);

	particles_draw(ctxt->particles, fragment);
}


/* Settings hooks for configurable variables */
static int sparkler_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*show_bsp_leafs;
	const char	*show_bsp_leafs_min_depth;
	const char	*show_bsp_matches;
	const char	*show_bsp_matches_affected_only;
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

	if (!strcasecmp(show_bsp_matches, "on")) {
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

	}

	if (res_setup) {
		sparkler_setup_t	*setup;

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		if (!strcasecmp(show_bsp_leafs, "on")) {
			setup->show_bsp_leafs = 1;

			sscanf(show_bsp_leafs_min_depth, "%u", &setup->show_bsp_leafs_min_depth);
		}

		if (!strcasecmp(show_bsp_matches, "on")) {
			setup->show_bsp_matches = 1;

			if (!strcasecmp(show_bsp_matches_affected_only, "on"))
				setup->show_bsp_matches_affected_only = 1;
		}

		*res_setup = &setup->til_setup;
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
