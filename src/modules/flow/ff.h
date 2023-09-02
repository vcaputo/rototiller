#ifndef _FF_H
#define _FF_H

#include "v3f.h"

typedef struct ff_t ff_t;

ff_t * ff_new(unsigned size, void (*populator)(void *context, unsigned size, const v3f_t *other, v3f_t *field), void *context);
void ff_free(ff_t *ff);
v3f_t ff_get(ff_t *ff, v3f_t *coordinate, float w);
void ff_populate(ff_t *ff, unsigned idx);

#endif
