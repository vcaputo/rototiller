#include <stdarg.h>
#include <stdlib.h>

#include "til_fb.h"

#include "burst.h"
#include "helpers.h"
#include "particle.h"
#include "particles.h"
#include "xplode.h"

/* a "rocket" particle type */
#define ROCKET_MAX_DECAY_RATE	20
#define ROCKET_MIN_DECAY_RATE	2
#define ROCKET_MAX_LIFETIME	500
#define ROCKET_MIN_LIFETIME	300
#define ROCKETS_MAX		20
#define ROCKETS_XPLODE_MIN_SIZE	2000
#define ROCKETS_XPLODE_MAX_SIZE	8000

extern particle_ops_t burst_ops;
extern particle_ops_t spark_ops;
extern particle_ops_t xplode_ops;

static unsigned rockets_cnt;

typedef struct rocket_ctxt_t {
	int		decay_rate;
	int		longevity;
	v3f_t		wander;
	float		last_velocity;	/* cache velocity to sense violent accelerations and explode when they happen */
} rocket_ctxt_t;

static unsigned xplode_colors[] = {
	0xffff00,
	0xff0000,
	0xff00ff,
	0x00ffff,
	0x0000ff,
	0x00ff00,
};

static int rocket_init(particles_t *particles, const particles_conf_t *conf, particle_t *p, unsigned n_params, va_list params)
{
	rocket_ctxt_t	*ctxt = p->ctxt;

	if (rockets_cnt >= ROCKETS_MAX) {
		return 0;
	}
	rockets_cnt++;

	ctxt->decay_rate = rand_within_range(conf->seedp, ROCKET_MIN_DECAY_RATE, ROCKET_MAX_DECAY_RATE);
	ctxt->longevity = rand_within_range(conf->seedp, ROCKET_MIN_LIFETIME, ROCKET_MAX_LIFETIME);

	ctxt->wander.x = (float)(rand_within_range(conf->seedp, 0, 628) - 314) * .0001;
	ctxt->wander.y = (float)(rand_within_range(conf->seedp, 0, 628) - 314) * .0001;
	ctxt->wander.z = (float)(rand_within_range(conf->seedp, 0, 628) - 314) * .0001;
	ctxt->wander = v3f_normalize(&ctxt->wander);

	ctxt->last_velocity = p->props->velocity;
	p->props->drag = 0.4;
	p->props->mass = 0.8;
	p->props->virtual = 0;

	return 1;
}


static particle_status_t rocket_sim(particles_t *particles, const particles_conf_t *conf, particle_t *p, til_fb_fragment_t *f)
{
	rocket_ctxt_t	*ctxt = p->ctxt;
	int		i, n_sparks;

	if (!ctxt->longevity ||
	    (ctxt->longevity -= ctxt->decay_rate) <= 0 ||
	    p->props->velocity - ctxt->last_velocity > p->props->velocity * .05) {	/* explode if accelerated too hard (burst) */
		int		n_xplode;
		unsigned	color = xplode_colors[rand_within_range(conf->seedp, 0, nelems(xplode_colors))];
		/* on death we explode */

		ctxt->longevity = 0;


		/* how many explosion particles? */
		n_xplode = rand_within_range(conf->seedp, ROCKETS_XPLODE_MIN_SIZE, ROCKETS_XPLODE_MAX_SIZE);

		/* add a burst shockwave particle at our location, scale force
		 * and radius according to explosion size.
		 */
		particles_spawn_particle(particles, p, NULL, &burst_ops, 1, BURST_PARAM_FORCE_FLOAT, (float)n_xplode * 0.00001f);

		/* add the explosion particles */
		for (i = 0; i < n_xplode; i++) {
			particle_props_t	props = *p->props;
			particle_ops_t		*ops = &xplode_ops;

			props.direction.x = ((float)(rand_within_range(conf->seedp, 0, 31415900 * 2) - 31415900) * .0000001);
			props.direction.y = ((float)(rand_within_range(conf->seedp, 0, 31415900 * 2) - 31415900) * .0000001);
			props.direction.z = ((float)(rand_within_range(conf->seedp, 0, 31415900 * 2) - 31415900) * .0000001);
			props.direction = v3f_normalize(&props.direction);

			//props->velocity = ((float)rand_within_range(100, 200) / 100000.0);
			props.velocity = ((float)rand_within_range(conf->seedp, 100, 400) * .00001);
			particles_spawn_particle(particles, p, &props, ops, 1, XPLODE_PARAM_COLOR_UINT, color);
		}
		return PARTICLE_DEAD;
	}

#if 1
	/* FIXME: this isn't behaving as intended */
	p->props->direction = v3f_add(&p->props->direction, &ctxt->wander);
	p->props->direction = v3f_normalize(&p->props->direction);
#endif
	p->props->velocity += .00003;

	/* spray some sparks behind the rocket */
	n_sparks = rand_within_range(conf->seedp, 10, 40);
	for (i = 0; i < n_sparks; i++) {
		particle_props_t	props = *p->props;

		props.direction = v3f_negate(&props.direction);

		props.direction.x += (float)(rand_within_range(conf->seedp, 0, 40) - 20) * .01;
		props.direction.y += (float)(rand_within_range(conf->seedp, 0, 40) - 20) * .01;
		props.direction.z += (float)(rand_within_range(conf->seedp, 0, 40) - 20) * .01;
		props.direction = v3f_normalize(&props.direction);

		props.velocity = (float)rand_within_range(conf->seedp, 10, 50) * .00001;
		particles_spawn_particle(particles, p, &props, &spark_ops, 0);
	}

	ctxt->last_velocity = p->props->velocity;

	return PARTICLE_ALIVE;
}


static void rocket_draw(particles_t *particles, const particles_conf_t *conf, particle_t *p, int x, int y, til_fb_fragment_t *f)
{
	rocket_ctxt_t	*ctxt = p->ctxt;

	if (!should_draw_expire_if_oob(particles, p, x, y, f, &ctxt->longevity))
		/* kill off parts that wander off screen */
		return;

	til_fb_fragment_put_pixel_unchecked(f, 0, x, y, 0xff0000);
}


static void rocket_cleanup(particles_t *particles, const particles_conf_t *conf, particle_t *p)
{
	rockets_cnt--;
}


particle_ops_t	rocket_ops = {
			.context_size = sizeof(rocket_ctxt_t),
			.sim = rocket_sim,
			.init = rocket_init,
			.draw = rocket_draw,
			.cleanup = rocket_cleanup,
		};
