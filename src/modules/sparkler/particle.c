#include <stdarg.h>

#include "particle.h"

/* convert a particle to a new type */
void particle_convert(particles_t *particles, const particles_conf_t *conf, particle_t *p, particle_props_t *props, particle_ops_t *ops, unsigned n_params, ...)
{
	va_list	ap;

	va_start(ap, n_params);

	particle_cleanup(particles, conf, p);
	if (props) {
		*p->props = *props;
	}
	if (ops) {
		p->ops = ops;
	}
	particle_init(particles, conf, p, n_params, ap);

	va_end(ap);
}
