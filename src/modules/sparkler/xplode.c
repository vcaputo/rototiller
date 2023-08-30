#include <stdarg.h>
#include <stdlib.h>

#include "til_fb.h"

#include "helpers.h"
#include "particle.h"
#include "particles.h"
#include "xplode.h"

/* a "xplode" particle type, emitted by rockets in large numbers at the end of their duration */

extern particle_ops_t spark_ops;
particle_ops_t	xplode_ops;

typedef struct _xplode_ctxt_t {
#define PARAMS_DECLARE_STRUCT
#include "xplode_params.def"
	int		remaining;
} xplode_ctxt_t;


static int xplode_init(particles_t *particles, const particles_conf_t *conf, particle_t *p, unsigned n_params, va_list params)
{
	xplode_ctxt_t	*ctxt = p->ctxt;

#define PARAMS_ASSIGN_DEFAULTS
#include "xplode_params.def"

	for (; n_params; n_params--) {
		switch (va_arg(params, xplode_param_t)) {
#define PARAMS_IMPLEMENT_SWITCH
#include "xplode_params.def"
		default:
			return 0;
		}
	}

	ctxt->remaining = ctxt->duration;
	p->props->drag = 10.5;
	p->props->mass = 0.3;
	p->props->virtual = 0;

	return 1;
}


static particle_status_t xplode_sim(particles_t *particles, const particles_conf_t *conf, particle_t *p, til_fb_fragment_t *f)
{
	xplode_ctxt_t	*ctxt = p->ctxt;

	if (!ctxt->remaining || (ctxt->remaining -= ctxt->decay_rate) <= 0) {
		ctxt->remaining = 0;
		return PARTICLE_DEAD;
	}

	/* litter some small sparks behind the explosion particle */
	if (!(ctxt->duration % 30)) {
		particle_props_t	props = *p->props;

		props.velocity = (float)rand_within_range(conf->seedp, 10, 50) * .0001;
		particles_spawn_particle(particles, p, &props, &xplode_ops, 1, XPLODE_PARAM_COLOR_UINT, ctxt->color);
	}

	return PARTICLE_ALIVE;
}


static void xplode_draw(particles_t *particles, const particles_conf_t *conf, particle_t *p, int x, int y, til_fb_fragment_t *f)
{
	xplode_ctxt_t	*ctxt = p->ctxt;
	uint32_t	color;

	if (!should_draw_expire_if_oob(particles, p, x, y, f, &ctxt->remaining))
		return;

	if (ctxt->remaining == ctxt->duration) {
		/* always start with a white flash */
		color = makergb(0xff, 0xff, 0xa0, 1.0);
	} else {
		color = makergb((ctxt->color & 0xff0000) >> 16, (ctxt->color & 0xff00) >> 8, ctxt->color & 0xff, ((float)ctxt->remaining / ctxt->duration));
	}

	til_fb_fragment_put_pixel_unchecked(f, 0, x, y, color);
}


particle_ops_t	xplode_ops = {
			.context_size = sizeof(xplode_ctxt_t),
			.sim = xplode_sim,
			.init = xplode_init,
			.draw = xplode_draw,
			.cleanup = NULL,
		};
