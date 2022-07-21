#ifndef _PARTICLES_H
#define _PARTICLES_H

#include "til_fb.h"

#include "bsp.h"
#include "list.h"
#include "particle.h"

typedef struct particles_conf_t {
	unsigned	show_bsp_leafs:1;
	unsigned	show_bsp_matches:1;
	unsigned	show_bsp_matches_affected_only:1;
	unsigned	show_bsp_leafs_min_depth;
	unsigned	*seedp;
} particles_conf_t;

typedef struct particles_t particles_t;
typedef struct v3f_t v3f_t;

particles_t * particles_new(const particles_conf_t *conf);
void particles_draw(particles_t *particles, til_fb_fragment_t *fragment);
particle_status_t particles_sim(particles_t *particles, til_fb_fragment_t *fragment);
void particles_age(particles_t *particles);
void particles_free(particles_t *particles);
int particles_add_particle(particles_t *particles, particle_props_t *props, particle_ops_t *ops);
void particles_spawn_particle(particles_t *particles, particle_t *parent, particle_props_t *props, particle_ops_t *ops);
void particles_add_particles(particles_t *particles, particle_props_t *props, particle_ops_t *ops, int num);
bsp_t * particles_bsp(particles_t *particles);
void particles_draw_line(particles_t *particles, const v3f_t *a, const v3f_t *b, til_fb_fragment_t *fragment);

#endif
