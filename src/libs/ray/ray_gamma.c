#include <math.h>

#include "ray_gamma.h"

void ray_gamma_prepare(float gamma, ray_gamma_t *res_gamma)
{
	if (res_gamma->gamma == gamma)
		return;

	/* This is from graphics gems 2 "REAL PIXELS" */
	for (unsigned i = 0; i < 1024; i++)
		res_gamma->table[i] = 256.0f * powf((((float)i + .5f) / 1024.0f), 1.0f/gamma);

	res_gamma->gamma = gamma;
}
