/* implements a classical perlin noise function */
/* https://en.wikipedia.org/wiki/Perlin_noise */

#include <stdlib.h>

#include "din.h"
#include "v3f.h"

typedef struct din_t {
	int	width, height, depth;
	v3f_t	grid[];
} din_t;


/* return random number between -1 and +1 */
static inline float randf(void)
{
	return 2.f / RAND_MAX * rand() - 1.f;
}


void din_randomize(din_t *din)
{
	int	x, y, z;

	for (z = 0; z < din->depth; z++) {
		for (y = 0; y < din->height; y++) {
			for (x = 0; x < din->width; x++) {
				v3f_t	r;

				r.x = randf();
				r.y = randf();
				r.z = randf();

				din->grid[z * din->width * din->height + y * din->width + x] = v3f_normalize(&r);
			}
		}
	}
}


din_t * din_new(int width, int height, int depth)
{
	din_t	*din;

	din = calloc(1, sizeof(din_t) + (sizeof(v3f_t) * (width * height * depth)));
	if (!din)
		return NULL;

	din->width = width;
	din->height = height;
	din->depth = depth;

	din_randomize(din);

	return din;
}


void din_free(din_t *din)
{
	free(din);
}


static inline float dotgradient(const din_t *din, int x, int y, int z, const v3f_t coordinate)
{
	v3f_t	distance = v3f_sub(&coordinate, &(v3f_t){.x = x, .y = y, .z = z});

	return v3f_dot(&din->grid[z * din->width * din->height + y * din->width + x], &distance);
}


static inline float lerp(float a, float b, float t)
{
	return (1.0f - t) * a + t * b;
}


static inline float clamp(float x, float lowerlimit, float upperlimit) {
	if (x < lowerlimit)
		x = lowerlimit;

	if (x > upperlimit)
		x = upperlimit;

	return x;
}


static inline float smootherstep(float edge0, float edge1, float x) {
	x = clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);

	return x * x * x * (x * (x * 6.f - 15.f) + 10.f);
}


/* coordinate is in a unit cube of -1...+1 */
float din(din_t *din, v3f_t coordinate)
{
	int	x0, y0, z0, x1, y1, z1;
	float	i1, i2, ii1, ii2;
	float	tx, ty, tz;
	float	n0, n1;

	coordinate.x = 1.f + (coordinate.x * .5f + .5f) * (float)(din->width - 2);
	coordinate.y = 1.f + (coordinate.y * .5f + .5f) * (float)(din->height - 2);
	coordinate.z = 1.f + (coordinate.z * .5f + .5f) * (float)(din->depth - 2);

	x0 = floorf(coordinate.x);
	y0 = floorf(coordinate.y);
	z0 = floorf(coordinate.z);

	x1 = x0 + 1.f;
	y1 = y0 + 1.f;
	z1 = z0 + 1.f;

	tx = coordinate.x - (float)x0;
	ty = coordinate.y - (float)y0;
	tz = coordinate.z - (float)z0;

	n0 = dotgradient(din, x0, y0, z0, coordinate);
	n1 = dotgradient(din, x1, y0, z0, coordinate);
	tx = smootherstep(0.f, 1.f, tx);
	i1 = lerp(n0, n1, tx);

	n0 = dotgradient(din, x0, y1, z0, coordinate);
	n1 = dotgradient(din, x1, y1, z0, coordinate);
	i2 = lerp(n0, n1, tx);

	ty = smootherstep(0.f, 1.f, ty);
	ii1 = lerp(i1, i2, ty);

	n0 = dotgradient(din, x0, y0, z1, coordinate);
	n1 = dotgradient(din, x1, y0, z1, coordinate);
	i1 = lerp(n0, n1, tx);

	n0 = dotgradient(din, x0, y1, z1, coordinate);
	n1 = dotgradient(din, x1, y1, z1, coordinate);
	i2 = lerp(n0, n1, tx);

	ii2 = lerp(i1, i2, ty);

	tz = smootherstep(0.f, 1.f, tz);

	return lerp(ii1, ii2, tz);
}
