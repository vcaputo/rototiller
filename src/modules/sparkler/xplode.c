#include <stdlib.h>

#include "draw.h"
#include "particle.h"
#include "particles.h"

/* a "xplode" particle type, emitted by rockets in large numbers at the end of their lifetime */
#define XPLODE_MAX_DECAY_RATE	10
#define XPLODE_MIN_DECAY_RATE	5
#define XPLODE_MAX_LIFETIME	150
#define XPLODE_MIN_LIFETIME	5

extern particle_ops_t spark_ops;
particle_ops_t	xplode_ops;

typedef struct _xplode_ctxt_t {
	int		decay_rate;
	int		longevity;
	int		lifetime;
} xplode_ctxt_t;


static int xplode_init(particles_t *particles, particle_t *p)
{
	xplode_ctxt_t	*ctxt = p->ctxt;

	ctxt->decay_rate = rand_within_range(XPLODE_MIN_DECAY_RATE, XPLODE_MAX_DECAY_RATE);
	ctxt->lifetime = ctxt->longevity = rand_within_range(XPLODE_MIN_LIFETIME, XPLODE_MAX_LIFETIME);

	p->props->drag = 10.9;
	p->props->mass = 0.3;

	return 1;
}


static particle_status_t xplode_sim(particles_t *particles, particle_t *p)
{
	xplode_ctxt_t	*ctxt = p->ctxt;

	if (!ctxt->longevity || (ctxt->longevity -= ctxt->decay_rate) <= 0) {
		ctxt->longevity = 0;
		return PARTICLE_DEAD;
	}

	/* litter some small sparks behind the explosion particle */
	if (!(ctxt->lifetime % 30)) {
		particle_props_t	props = *p->props;

		props.velocity = (float)rand_within_range(10, 50) / 10000.0;
		particles_spawn_particle(particles, p, &props, &xplode_ops);
	}

	return PARTICLE_ALIVE;
}


static void xplode_draw(particles_t *particles, particle_t *p, int x, int y, fb_fragment_t *f)
{
	xplode_ctxt_t	*ctxt = p->ctxt;
	uint32_t	color;

	if (ctxt->longevity == ctxt->lifetime) {
		color = makergb(0xff, 0xff, 0xa0, 1.0);
	} else {
		color = makergb(0xff, 0xff, 0x00, ((float)ctxt->longevity / ctxt->lifetime));
	}

	if (!draw_pixel(f, x, y, color)) {
		/* offscreen */
		ctxt->longevity = 0;
	}
}


particle_ops_t	xplode_ops = {
			.context_size = sizeof(xplode_ctxt_t),
			.sim = xplode_sim,
			.init = xplode_init,
			.draw = xplode_draw,
			.cleanup = NULL,
		};
