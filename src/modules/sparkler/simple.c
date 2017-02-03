#include <stdlib.h>

#include "draw.h"
#include "fb.h"
#include "particle.h"
#include "particles.h"


/* a "simple" particle type */
#define SIMPLE_MAX_DECAY_RATE	20
#define SIMPLE_MIN_DECAY_RATE	2
#define SIMPLE_MAX_LIFETIME	110
#define SIMPLE_MIN_LIFETIME	30
#define SIMPLE_MAX_SPAWN	15
#define SIMPLE_MIN_SPAWN	2

extern particle_ops_t rocket_ops;

typedef struct _simple_ctxt_t {
	int		decay_rate;
	int		longevity;
	int		lifetime;
} simple_ctxt_t;


static int simple_init(particles_t *particles, particle_t *p)
{
	simple_ctxt_t	*ctxt = p->ctxt;

	ctxt->decay_rate = rand_within_range(SIMPLE_MIN_DECAY_RATE, SIMPLE_MAX_DECAY_RATE);
	ctxt->lifetime = ctxt->longevity = rand_within_range(SIMPLE_MIN_LIFETIME, SIMPLE_MAX_LIFETIME);

	if (!p->props->of_use) {
		/* everything starts from the bottom center */
		p->props->position.x = 0;
		p->props->position.y = 0;
		p->props->position.z = 0;

		/* TODO: direction random-ish within the range of a narrow upward facing cone */
		p->props->direction.x = (float)(rand_within_range(0, 6) - 3) * .1f;
		p->props->direction.y = 1.0f + (float)(rand_within_range(0, 6) - 3) * .1f;
		p->props->direction.z = (float)(rand_within_range(0, 6) - 3) * .1f;
		p->props->direction = v3f_normalize(&p->props->direction);

		p->props->velocity = (float)rand_within_range(300, 800) / 100000.0;

		p->props->drag = 0.03;
		p->props->mass = 0.3;
		p->props->of_use = 1;
	} /* else { we've been given properties, manipulate them or run with them? } */

	return 1;
}


static particle_status_t simple_sim(particles_t *particles, particle_t *p)
{
	simple_ctxt_t	*ctxt = p->ctxt;

	/* a particle is free to manipulate its children list when aging, but not itself or its siblings */
	/* return PARTICLE_DEAD to remove kill yourself, do not age children here, the age pass will recurse
	 * into children and age them independently _after_ their parents have been aged
	 */
	if (!ctxt->longevity || (ctxt->longevity -= ctxt->decay_rate) <= 0) {
		ctxt->longevity = 0;
		return PARTICLE_DEAD;
	}

	/* create particles inheriting our type based on some silly conditions, with some tweaks to their direction */
	if (ctxt->longevity == 42 || (ctxt->longevity > 500 && !(ctxt->longevity % 50))) {
		int	i, num = rand_within_range(SIMPLE_MIN_SPAWN, SIMPLE_MAX_SPAWN);

		for (i = 0; i < num; i++) {
			particle_props_t	props = *p->props;
			particle_ops_t		*ops = INHERIT_OPS;

			if (i == (SIMPLE_MAX_SPAWN - 2)) {
				ops = &rocket_ops;
				props.velocity = (float)rand_within_range(60, 100) / 1000000.0;
			} else {
				props.velocity = (float)rand_within_range(30, 100) / 10000.0;
			}

			props.direction.x += (float)(rand_within_range(0, 315 * 2) - 315) / 100.0;
			props.direction.y += (float)(rand_within_range(0, 315 * 2) - 315) / 100.0;
			props.direction.z += (float)(rand_within_range(0, 315 * 2) - 315) / 100.0;
			props.direction = v3f_normalize(&props.direction);

			particles_spawn_particle(particles, p, &props, ops); // XXX
		}
	}

	return PARTICLE_ALIVE;
}


static void simple_draw(particles_t *particles, particle_t *p, int x, int y, fb_fragment_t *f)
{
	simple_ctxt_t	*ctxt = p->ctxt;

	if (!fb_fragment_contains(f, x, y)) {
		/* immediately kill off stars that wander off screen */
		ctxt->longevity = 0;

		return;
	}

	draw_pixel(f, x, y, makergb(0xff, 0xff, 0xff, ((float)ctxt->longevity / ctxt->lifetime)));
}


particle_ops_t	simple_ops = {
			.context_size = sizeof(simple_ctxt_t),
			.sim = simple_sim,
			.init = simple_init,
			.draw = simple_draw,
			.cleanup = NULL,
		};
