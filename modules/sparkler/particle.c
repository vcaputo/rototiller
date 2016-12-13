#include "particle.h"

/* convert a particle to a new type */
void particle_convert(particles_t *particles, particle_t *p, particle_props_t *props, particle_ops_t *ops)
{
	particle_cleanup(particles, p);
	if (props) {
		*p->props = *props;
	}
	if (ops) {
		p->ops = ops;
	}
	particle_init(particles, p);
}
