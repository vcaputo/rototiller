#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "fb.h"

#include "chunker.h"
#include "container.h"
#include "bsp.h"
#include "list.h"
#include "particle.h"
#include "particles.h"
#include "v3f.h"

#define ZCONST	0.4f

/* private particle with all the particles bookkeeping... */
typedef struct _particle_t {
	list_head_t		siblings;	/* sibling particles */
	list_head_t		children;	/* children particles */

	particle_props_t	props;		/* we reference this in the public particle, I might change
						 * the way props are allocated so coding everything to use a
						 * reference for now.  It may make sense to have props allocated
						 * separately via their own chunker, and perform some mass operations
						 * against the list of chunks rather than chasing the pointers of
						 * the particle heirarchy. TODO
						 */
	particle_t		public;		/* the public particle_t is embedded */

	uint8_t			context[];	/* particle type-specific context [public.ops.context_size] */
} _particle_t;

struct particles_t {
	chunker_t		*chunker;	/* chunker for variably-sized particle allocation (includes context) */
	list_head_t		active;		/* top-level active list of particles heirarchy */
	bsp_t			*bsp;		/* bsp spatial index of the particles */
};


/* create a new particle system */
particles_t * particles_new(void)
{
	particles_t	*particles;

	particles = calloc(1, sizeof(particles_t));
	if (!particles) {
		return NULL;
	}

	particles->chunker = chunker_new(sizeof(_particle_t) * 128);
	if (!particles->chunker) {
		return NULL;
	}

	particles->bsp = bsp_new();
	if (!particles->bsp) {
		return NULL;
	}

	INIT_LIST_HEAD(&particles->active);

	return particles;
}


/* TODO: add a public interface for destroying particles?  for now we just return PARTICLE_DEAD in the sim */
static inline void _particles_free_particle(particles_t *particles, _particle_t *p)
{
	assert(p);

	particle_cleanup(particles, &p->public);
	chunker_free(p);
}


static inline void _particles_free(particles_t *particles, list_head_t *list)
{
	_particle_t	*p, *_p;

	assert(particles);
	assert(list);

	list_for_each_entry_safe(p, _p, list, siblings) {
		_particles_free(particles, &p->children);
		_particles_free_particle(particles, p);
	}
}


/* free up all the particles */
void particles_free(particles_t *particles)
{
	assert(particles);

	_particles_free(particles, &particles->active);
}


/* reclaim a dead particle, moving it to the free list */
static void particles_reap_particle(particles_t *particles, _particle_t *particle)
{
	assert(particles);
	assert(particle);

	if (!list_empty(&particle->children)) {
		/* adopt any orphaned children using the global parts list */
		list_splice(&particle->children, &particles->active);
	}

	list_del(&particle->siblings);
	bsp_delete_occupant(particles->bsp, &particle->public.occupant);
	_particles_free_particle(particles, particle);
}


/* add a particle to the specified list */
static inline int _particles_add_particle(particles_t *particles, list_head_t *list, particle_props_t *props, particle_ops_t *ops)
{
	_particle_t	*p;

	assert(particles);
	assert(ops);
	assert(list);

	p = chunker_alloc(particles->chunker, sizeof(_particle_t) + ops->context_size);
	if (!p) {
		return 0;
	}

	INIT_LIST_HEAD(&p->children);
	INIT_LIST_HEAD(&p->siblings);

	/* inherit the parent's properties and ops if they're not explicitly provided */
	if (props) {
		p->props = *props;
	} else {
		p->props.of_use = 0;
	}

	p->public.props = &p->props;
	p->public.ops = ops;

	if (ops->context_size) {
		p->public.ctxt = p->context;
	}

	if (!particle_init(particles, &p->public)) {
		/* XXX FIXME this shouldn't be normal, we don't want to allocate
		 * particles that cannot be initialized.  the rockets today set a cap
		 * by failing initialization, that's silly. */
		chunker_free(p);
		return 0;
	}

	p->public.props->of_use = 1;
	list_add(&p->siblings, list);
	bsp_add_occupant(particles->bsp, &p->public.occupant, &p->props.position);

	return 1;
}


/* add a new "top-level" particle of the specified props and ops taking from the provided parts list */
int particles_add_particle(particles_t *particles, particle_props_t *props, particle_ops_t *ops)
{
	assert(particles);

	return _particles_add_particle(particles, &particles->active, props, ops);
}


/* spawn a new child particle from a parent, initializing it via inheritance if desired */
void particles_spawn_particle(particles_t *particles, particle_t *parent, particle_props_t *props, particle_ops_t *ops)
{
	_particle_t	*p = container_of(parent, _particle_t, public);

	assert(particles);
	assert(parent);

	_particles_add_particle(particles, &p->children, props ? props : parent->props, ops ? ops : parent->ops);
}


/* plural version of particle_add(); adds multiple "top-level" particles of uniform props and ops */
void particles_add_particles(particles_t *particles, particle_props_t *props, particle_ops_t *ops, int num)
{
	int	i;

	assert(particles);

	for (i = 0; i < num; i++) {
		_particles_add_particle(particles, &particles->active, props, ops);
	}
}


/* Simple accessor to get the bsp pointer, the bsp is special because we don't want to do
 * callbacks per-occupant, so the bsp_occupant_t and search functions are used directly by
 * the per-particle code needing nearest-neighbor search.  that requires an accessor since
 * particles_t is opaque.  This seemed less shitty than opening up particles_t.
 */
bsp_t * particles_bsp(particles_t *particles)
{
	assert(particles);
	assert(particles->bsp);

	return particles->bsp;
}


static inline void _particles_draw(particles_t *particles, list_head_t *list, fb_fragment_t *fragment)
{
	float		w2 = fragment->frame_width * .5f, h2 = fragment->frame_height * .5f;
	_particle_t	*p;

	assert(particles);
	assert(list);
	assert(fragment);

	list_for_each_entry(p, list, siblings) {
		int	x, y;

		/* project the 3d coordinates onto the 2d plane */
		x = (p->props.position.x / (p->props.position.z - ZCONST) * w2) + w2;
		y = (p->props.position.y / (p->props.position.z - ZCONST) * h2) + h2;

		particle_draw(particles, &p->public, x, y, fragment);

		if (!list_empty(&p->children)) {
			_particles_draw(particles, &p->children, fragment);
		}
	}
}


/* draw all of the particles, currently called in heirarchical order */
void particles_draw(particles_t *particles, fb_fragment_t *fragment)
{
	assert(particles);

	_particles_draw(particles, &particles->active, fragment);
}


static inline particle_status_t _particles_sim(particles_t *particles, list_head_t *list)
{
	particle_status_t	ret = PARTICLE_DEAD, s;
	_particle_t		*p, *_p;

	assert(particles);
	assert(list);

	list_for_each_entry_safe(p, _p, list, siblings) {
		if ((s = particle_sim(particles, &p->public)) == PARTICLE_ALIVE) {
			ret = PARTICLE_ALIVE;

			if (!list_empty(&p->children) &&
			    _particles_sim(particles, &p->children) == PARTICLE_ALIVE) {
				ret = PARTICLE_ALIVE;
			}
		} else {
			particles_reap_particle(particles, p);
		}
	}

	return ret;
}


/* simulate the particles, call the sim method of every particle in the heirarchy, this is what makes the particles dynamic */
/* if any paticle is still living, we return PARTICLE_ALIVE, to inform the caller when everything's dead */
particle_status_t particles_sim(particles_t *particles)
{
	assert(particles);

	return _particles_sim(particles, &particles->active);
}


/* "age" the particle by applying its properties for one step */
static inline void _particle_age(particles_t *particles, _particle_t *p)
{
#if 1
	if (p->props.mass > 0.0f) {
		/* gravity, TODO: mass isn't applied. */
		static v3f_t	gravity = v3f_init(0.0f, -0.05f, 0.0f);

		p->props.direction = v3f_add(&p->props.direction, &gravity);
		p->props.direction = v3f_normalize(&p->props.direction);
	}
#endif

#if 1
	/* some drag/resistance proportional to velocity TODO: integrate mass */
	if (p->props.velocity > 0.0f) {
		p->props.velocity -= ((p->props.velocity * p->props.velocity * p->props.drag));
		if (p->props.velocity < 0.0f) {
			p->props.velocity = 0;
		}
	}
#endif

	/* regular movement */
	if (p->props.velocity > 0.0f) {
		v3f_t	movement = v3f_mult_scalar(&p->props.direction, p->props.velocity);

		p->props.position = v3f_add(&p->props.position, &movement);
		bsp_move_occupant(particles->bsp, &p->public.occupant, &p->props.position);
	}
}


static void _particles_age(particles_t *particles, list_head_t *list)
{
	_particle_t	*p;

	assert(particles);
	assert(list);

	/* TODO: since this *only* involves the properties struct, if they were
	 * allocated from a separate slab containing only properties, it'd be
	 * more efficient to iterate across property arrays and skip inactive
	 * entries.  This heirarchical pointer-chasing recursion isn't
	 * particularly good for cache utilization.
	 */
	list_for_each_entry(p, list, siblings) {

		if (!p->props.virtual) {
			_particle_age(particles, p);
		}

		if (!list_empty(&p->children)) {
			_particles_age(particles, &p->children);
		}
	}
}


/* advance time for all the particles (move them), this doesn't currently invoke any part-specific helpers, it's just applying
 * physics-type stuff, moving particles according to their velocities, directions, mass, drag, gravity etc... */
void particles_age(particles_t *particles)
{
	assert(particles);

	_particles_age(particles, &particles->active);
}
