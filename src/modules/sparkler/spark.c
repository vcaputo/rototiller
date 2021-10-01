#include <stdlib.h>

#include "til_fb.h"

#include "helpers.h"
#include "particle.h"
#include "particles.h"

/* a "spark" particle type, emitted from behind rockets */
#define SPARK_MAX_DECAY_RATE	20
#define SPARK_MIN_DECAY_RATE	2
#define SPARK_MAX_LIFETIME	150
#define SPARK_MIN_LIFETIME	1

typedef struct _spark_ctxt_t {
	int		decay_rate;
	int		longevity;
	int		lifetime;
} spark_ctxt_t;


static int spark_init(particles_t *particles, const particles_conf_t *conf, particle_t *p)
{
	spark_ctxt_t	*ctxt = p->ctxt;

	p->props->drag = 20.0;
	p->props->mass = 0.1;
	p->props->virtual = 0;
	ctxt->decay_rate = rand_within_range(SPARK_MIN_DECAY_RATE, SPARK_MAX_DECAY_RATE);
	ctxt->lifetime = ctxt->longevity = rand_within_range(SPARK_MIN_LIFETIME, SPARK_MAX_LIFETIME);

	return 1;
}


static particle_status_t spark_sim(particles_t *particles, const particles_conf_t *conf, particle_t *p, til_fb_fragment_t *f)
{
	spark_ctxt_t	*ctxt = p->ctxt;

	if (!ctxt->longevity || (ctxt->longevity -= ctxt->decay_rate) <= 0) {
		ctxt->longevity = 0;
		return PARTICLE_DEAD;
	}

	return PARTICLE_ALIVE;
}


static void spark_draw(particles_t *particles, const particles_conf_t *conf, particle_t *p, int x, int y, til_fb_fragment_t *f)
{
	spark_ctxt_t	*ctxt = p->ctxt;

	if (!should_draw_expire_if_oob(particles, p, x, y, f, &ctxt->longevity))
		/* offscreen */
		return;

	til_fb_fragment_put_pixel_unchecked(f, x, y, makergb(0xff, 0xa0, 0x20, ((float)ctxt->longevity / ctxt->lifetime)));
}


particle_ops_t	spark_ops = {
			.context_size = sizeof(spark_ctxt_t),
			.sim = spark_sim,
			.init = spark_init,
			.draw = spark_draw,
			.cleanup = NULL,
		};
