#ifndef _FF_H
#define _FF_H

#include "v3f.h"

typedef struct ff_t ff_t;

typedef struct ff_data_t {
	v3f_t	direction;
	v3f_t	color;
} ff_data_t;

ff_t * ff_new(unsigned size, void (*populator)(void *context, unsigned size, const ff_data_t *other, ff_data_t *field), void *context);
ff_t * ff_free(ff_t *ff);
ff_data_t ff_get(ff_t *ff, v3f_t *coordinate, float w);
void ff_populate(ff_t *ff, unsigned idx);

#endif
