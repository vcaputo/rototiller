#ifndef _DIN_H
#define _DIN_H

#include "v3f.h"

typedef struct din_t din_t;

din_t * din_new(int width, int height, int depth);
void din_free(din_t *din);
void din_randomize(din_t *din);
float din(din_t *din, v3f_t coordinate);

#endif
