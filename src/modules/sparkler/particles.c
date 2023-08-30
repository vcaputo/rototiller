#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "til_fb.h"

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
	particles_conf_t	conf;
};


/* create a new particle system */
particles_t * particles_new(const particles_conf_t *conf)
{
	particles_t	*particles;

	assert(conf && conf->seedp);

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

	particles->conf = *conf;

	return particles;
}


/* TODO: add a public interface for destroying particles?  for now we just return PARTICLE_DEAD in the sim */
static inline void _particles_free_particle(particles_t *particles, _particle_t *p)
{
	assert(p);

	particle_cleanup(particles, &particles->conf, &p->public);
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
	bsp_free(particles->bsp);
	chunker_free_chunker(particles->chunker);
	free(particles);
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
static inline int _particles_add_particle(particles_t *particles, list_head_t *list, particle_props_t *props, particle_ops_t *ops, unsigned n_params, va_list ap)
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

	if (!particle_init(particles, &particles->conf, &p->public, n_params, ap)) {
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
int particles_add_particle(particles_t *particles, particle_props_t *props, particle_ops_t *ops, unsigned n_params, ...)
{
	int	ret;
	va_list	ap;

	assert(particles);

	va_start(ap, n_params);
	ret = _particles_add_particle(particles, &particles->active, props, ops, n_params, ap);
	va_end(ap);

	return ret;
}


/* spawn a new child particle from a parent, initializing it via inheritance if desired */
void particles_spawn_particle(particles_t *particles, particle_t *parent, particle_props_t *props, particle_ops_t *ops, unsigned n_params, ...)
{
	va_list		ap;
	_particle_t	*p = container_of(parent, _particle_t, public);

	assert(particles);
	assert(parent);

	va_start(ap, n_params);
	_particles_add_particle(particles, &p->children, props ? props : parent->props, ops ? ops : parent->ops, n_params, ap);
	va_end(ap);
}


/* plural version of particle_add(); adds multiple "top-level" particles of uniform props and ops */
void particles_add_particles(particles_t *particles, particle_props_t *props, particle_ops_t *ops, unsigned num, unsigned n_params, ...)
{
	va_list	ap;
	int	i;

	assert(particles);

	va_start(ap, n_params);
	for (i = 0; i < num; i++) {
		_particles_add_particle(particles, &particles->active, props, ops, n_params, ap);
	}
	va_end(ap);
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


static inline void _particles_draw(particles_t *particles, list_head_t *list, til_fb_fragment_t *fragment)
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

		particle_draw(particles, &particles->conf, &p->public, x, y, fragment);

		if (!list_empty(&p->children)) {
			_particles_draw(particles, &p->children, fragment);
		}
	}
}


/* TODO: maybe polish up and move into fb.c? */
static void draw_line(til_fb_fragment_t *fragment, int x1, int y1, int x2, int y2)
{
	int	x_delta = x2 - x1;
	int	y_delta = y2 - y1;
	int	sdx = x_delta < 0 ? -1 : 1;
	int	sdy = y_delta < 0 ? -1 : 1;

	x_delta = abs(x_delta);
	y_delta = abs(y_delta);

	if (x_delta >= y_delta) {
		/* X-major */
		for (int minor = 0, x = 0; x <= x_delta; x++, x1 += sdx, minor += y_delta) {
			if (minor >= x_delta) {
				y1 += sdy;
				minor -= x_delta;
			}

			til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x1, y1, 0xffffffff);
		}
	} else {
		/* Y-major */
		for (int minor = 0, y = 0; y <= y_delta; y++, y1 += sdy, minor += x_delta) {
			if (minor >= y_delta) {
				x1 += sdx;
				minor -= y_delta;
			}

			til_fb_fragment_put_pixel_checked(fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, x1, y1, 0xffffffff);
		}
	}
}


static void draw_edge(til_fb_fragment_t *fragment, const v3f_t *a, const v3f_t *b)
{
	float	w2 = fragment->frame_width * .5f, h2 = fragment->frame_height * .5f;
	int	x1, y1, x2, y2;

	/* project the 3d coordinates onto the 2d plane */
	x1 = (a->x / (a->z - ZCONST) * w2) + w2;
	y1 = (a->y / (a->z - ZCONST) * h2) + h2;
	x2 = (b->x / (b->z - ZCONST) * w2) + w2;
	y2 = (b->y / (b->z - ZCONST) * h2) + h2;

	draw_line(fragment, x1, y1, x2, y2);
}


static void draw_bv(til_fb_fragment_t *fragment, const v3f_t *bv_min, const v3f_t *bv_max)
{
	draw_edge(fragment,
		&(v3f_t){bv_min->x, bv_max->y, bv_min->z},
		&(v3f_t){bv_max->x, bv_max->y, bv_min->z});
	draw_edge(fragment,
		&(v3f_t){bv_min->x, bv_max->y, bv_min->z},
		&(v3f_t){bv_min->x, bv_max->y, bv_max->z});
	draw_edge(fragment,
		&(v3f_t){bv_min->x, bv_max->y, bv_min->z},
		&(v3f_t){bv_min->x, bv_min->y, bv_min->z});
	draw_edge(fragment,
		&(v3f_t){bv_max->x, bv_min->y, bv_min->z},
		&(v3f_t){bv_max->x, bv_min->y, bv_max->z});
	draw_edge(fragment,
		&(v3f_t){bv_max->x, bv_min->y, bv_min->z},
		&(v3f_t){bv_min->x, bv_min->y, bv_min->z});
	draw_edge(fragment,
		&(v3f_t){bv_max->x, bv_min->y, bv_min->z},
		&(v3f_t){bv_max->x, bv_max->y, bv_min->z});
	draw_edge(fragment,
		&(v3f_t){bv_max->x, bv_max->y, bv_max->z},
		&(v3f_t){bv_min->x, bv_max->y, bv_max->z});
	draw_edge(fragment,
		&(v3f_t){bv_max->x, bv_max->y, bv_max->z},
		&(v3f_t){bv_max->x, bv_max->y, bv_min->z});
	draw_edge(fragment,
		&(v3f_t){bv_max->x, bv_max->y, bv_max->z},
		&(v3f_t){bv_max->x, bv_min->y, bv_max->z});
	draw_edge(fragment,
		&(v3f_t){bv_min->x, bv_min->y, bv_max->z},
		&(v3f_t){bv_min->x, bv_min->y, bv_min->z});
	draw_edge(fragment,
		&(v3f_t){bv_min->x, bv_min->y, bv_max->z},
		&(v3f_t){bv_min->x, bv_max->y, bv_max->z});
	draw_edge(fragment,
		&(v3f_t){bv_min->x, bv_min->y, bv_max->z},
		&(v3f_t){bv_max->x, bv_min->y, bv_max->z});
}


/* something to encapsulate these pointers for passing through as one to draw_leaf() */
typedef struct draw_leafs_t {
	particles_t		*particles;
	til_fb_fragment_t	*fragment;
} draw_leafs_t;


/* callback for bsp_walk_leaves() when show_bsp_leafs is enabled */
static void draw_leaf(const bsp_t *bsp, const list_head_t *occupants, unsigned depth, const v3f_t *bv_min, const v3f_t *bv_max, void *cb_data)
{
	draw_leafs_t	*draw = cb_data;

	if (list_empty(occupants))
		return;

	if (depth < draw->particles->conf.show_bsp_leafs_min_depth)
		return;

	draw_bv(draw->fragment, bv_min, bv_max);
}


/* draw all of the particles, currently called in heirarchical order */
void particles_draw(particles_t *particles, til_fb_fragment_t *fragment)
{
	draw_leafs_t	draw = { .particles = particles, .fragment = fragment };

	assert(particles);

	_particles_draw(particles, &particles->active, fragment);

	if (particles->conf.show_bsp_leafs)
		bsp_walk_leaves(particles->bsp, draw_leaf, &draw);
}


static inline particle_status_t _particles_sim(particles_t *particles, list_head_t *list, til_fb_fragment_t *fragment)
{
	particle_status_t	ret = PARTICLE_DEAD, s;
	_particle_t		*p, *_p;

	assert(particles);
	assert(list);

	list_for_each_entry_safe(p, _p, list, siblings) {
		if ((s = particle_sim(particles, &particles->conf, &p->public, fragment)) == PARTICLE_ALIVE) {
			ret = PARTICLE_ALIVE;

			if (!list_empty(&p->children) &&
			    _particles_sim(particles, &p->children, fragment) == PARTICLE_ALIVE) {
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
particle_status_t particles_sim(particles_t *particles, til_fb_fragment_t *fragment)
{
	assert(particles);

	return _particles_sim(particles, &particles->active, fragment);
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


/* draw a line expressed in world-space positions a to b into fragment, this is intended for
 * instrumentation/overlay debugging type purposes...
 */
void particles_draw_line(particles_t *particles, const v3f_t *a, const v3f_t *b, til_fb_fragment_t *fragment)
{
	float	w2 = fragment->frame_width * .5f, h2 = fragment->frame_height * .5f;
	int	x1, y1, x2, y2;

	/* project the 3d coordinates onto the 2d plane */
	x1 = (a->x / (a->z - ZCONST) * w2) + w2;
	y1 = (a->y / (a->z - ZCONST) * h2) + h2;
	x2 = (b->x / (b->z - ZCONST) * w2) + w2;
	y2 = (b->y / (b->z - ZCONST) * h2) + h2;

	draw_line(fragment, x1, y1, x2, y2);
}
