/*
 * a starfield simulation library from: https://github.com/ph1l/stars
 * Copyright 2014 Philip J. Freeman <elektron@halo.nu>
 */

#include <stdlib.h>
#ifdef DEBUG
#include <stdio.h>
#endif
#include "starslib.h"

struct points
{
	int x, y, z;
	struct points *next;
};

void new_universe( struct universe** u, int width, int height, int depth )
{
	*u = malloc(sizeof(struct universe));

	(*u)->width  = width;
	(*u)->height = height;
	(*u)->depth  = depth;

	(*u)->iterator  = NULL;
	(*u)->points  = NULL;
	#ifdef DEBUG
	printf("NEW UNIVERSE: %lx: (%i,%i,%i)\n", (long unsigned int )(*u), (*u)->width, (*u)->height, (*u)->depth);
	#endif

	return;
}

void new_point( struct universe* u )
{

	struct points* p_ptr = malloc(sizeof(struct points));

	p_ptr->x    = (rand()%u->width - (u->width/2)) * u->depth;
	p_ptr->y    = (rand()%u->height - (u->height/2)) * u->depth;
	p_ptr->z    = u->depth;

	p_ptr->next = u->points;
	u->points = p_ptr;
	#ifdef DEBUG
	printf("NEW POINT: %lx: (%i,%i,%i) next=%lx\n", (long unsigned int )p_ptr, p_ptr->x, p_ptr->y, p_ptr->z, (long unsigned int) p_ptr->next);
	#endif

	return;
}

void kill_point( struct universe* universe, struct points* to_kill )
{

	struct points *p_ptr, *last_ptr = NULL;


	for ( p_ptr = universe->points; p_ptr != NULL; p_ptr = p_ptr->next)
	{
		if (p_ptr == to_kill)
		{
			#ifdef DEBUG
			printf("KILL POINT: %lx: (%i,%i,%i).\n", (long unsigned int )p_ptr, p_ptr->x, p_ptr->y, p_ptr->z);
			#endif
			if (last_ptr == NULL)
			{
				universe->points = p_ptr->next;
			} else {
				last_ptr->next = p_ptr->next;
			}
			free(p_ptr);
		} else {
			last_ptr = p_ptr;
		}
	}
	#ifdef DEBUG
	printf("KILL POINT: %lx\n", (long unsigned int )p_ptr);
	#endif
	return;
}

int process_point( struct universe *u, struct return_point *rp )
{

	if ( u->iterator == NULL ) {
		if (u->points == NULL){
			return 0;
		} else {
			u->iterator = u->points;
		}
	}

	if ( u->iterator->z == 0 ){
		// Delete point that has reached us.
		struct points *tmp = u->iterator;
		u->iterator = u->iterator->next;
		kill_point( u, tmp );
		return(-1);
	} else {
		// Plot the point
		int x, y;
		x = u->iterator->x / u->iterator->z;
		y = u->iterator->y / u->iterator->z;
		if ( abs(x) >= u->width/2 || abs(y) >= u->height/2 ){
			// Delete point that is off screen
			struct points *tmp = u->iterator;
			u->iterator = u->iterator->next;
			kill_point( u, tmp );
			if ( u->iterator == NULL ) {
				return(0);
			} else {
				return(-1);
			}
		} else {
			int m = OPACITY_MAX*((u->depth-u->iterator->z)*4)/u->depth;
			if ( m>OPACITY_MAX ){ m=OPACITY_MAX; }
			u->iterator->z = u->iterator->z - 1;
			#ifdef DEBUG
			printf("RETURN POINT: %lx\n", (long unsigned int )u->iterator);
			#endif
			u->iterator = u->iterator->next;
			rp->x = x;
			rp->y = y;
			rp->opacity = m;
			if ( u->iterator == NULL ) {
				return(0);
			} else {
				return(1);
			}
		}
	}
}
