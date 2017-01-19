#ifndef _PARTICLES_H
#define _PARTICLES_H

#include "bsp.h"
#include "fb.h"
#include "list.h"
#include "particle.h"

typedef struct particles_t particles_t;

particles_t * particles_new(void);
void particles_draw(particles_t *particles, fb_fragment_t *fragment);
particle_status_t particles_sim(particles_t *particles);
void particles_age(particles_t *particles);
void particles_free(particles_t *particles);
int particles_add_particle(particles_t *particles, particle_props_t *props, particle_ops_t *ops);
void particles_spawn_particle(particles_t *particles, particle_t *parent, particle_props_t *props, particle_ops_t *ops);
void particles_add_particles(particles_t *particles, particle_props_t *props, particle_ops_t *ops, int num);
bsp_t * particles_bsp(particles_t *particles);

#endif
