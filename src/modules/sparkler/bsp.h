#ifndef _BSP_H
#define _BSP_H

#include <stdint.h>

#include "list.h"
#include "v3f.h"

typedef struct bsp_t bsp_t;
typedef struct bsp_node_t bsp_node_t;

/* Embed this in anything you want spatially indexed by the bsp tree. */
/* TODO: it would be nice to make this opaque, but it's a little annoying. */
typedef struct bsp_occupant_t {
	bsp_node_t	*leaf;		/* leaf node containing this occupant */
	list_head_t	occupants;	/* node on containing leaf node's list of occupants */
	v3f_t		*position;	/* position of occupant to be partitioned */
} bsp_occupant_t;

bsp_t * bsp_new(void);
void bsp_free(bsp_t *bsp);
void bsp_print(bsp_t *bsp);
void bsp_add_occupant(bsp_t *bsp, bsp_occupant_t *occupant, v3f_t *position);
void bsp_delete_occupant(bsp_t *bsp, bsp_occupant_t *occupant);
void bsp_move_occupant(bsp_t *bsp, bsp_occupant_t *occupant, v3f_t *position);
void bsp_search_sphere(bsp_t *bsp, v3f_t *center, float radius_min, float radius_max, void (*cb)(bsp_t *, list_head_t *, void *), void *cb_data);

#endif
