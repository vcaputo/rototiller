#ifndef _DIN_H
#define _DIN_H

typedef struct din_t din_t;
typedef struct v3f_t v3f_t;

din_t * din_new(int width, int height, int depth, unsigned seed);
void din_free(din_t *din);
void din_randomize(din_t *din);
float din(din_t *din, v3f_t *coordinate);

#endif
