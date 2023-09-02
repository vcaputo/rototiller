#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "ff.h"
#include "v3f.h"

typedef struct ff_t {
	unsigned	size;
	v3f_t		*fields[2];
	void		(*populator)(void *context, unsigned size, const v3f_t *other, v3f_t *field);
	void		*populator_context;
} ff_t;


/* populate the flow field specified by idx */
void ff_populate(ff_t *ff, unsigned idx)
{
	unsigned	other;

	assert(idx < 2);

	other = (idx + 1) % 2;

	ff->populator(ff->populator_context, ff->size, ff->fields[other], ff->fields[idx]);
}


ff_t * ff_new(unsigned size, void (*populator)(void *context, unsigned size, const v3f_t *other, v3f_t *field), void *context)
{
	ff_t		*ff;

	ff = calloc(1, sizeof(ff_t));
	if (!ff)
		return NULL;

	for (int i = 0; i < 2; i++) {
		ff->fields[i] = calloc(size * size * size, sizeof(v3f_t));
		if (!ff->fields[i]) {
			for (int j = 0; j < i; j++)
				free(ff->fields[j]);

			free(ff);
			return NULL;
		}
	}

	ff->size = size;
	ff->populator = populator;
	ff->populator_context = context;

	for (unsigned i = 0; i < 2; i++)
		ff_populate(ff, i);

	return ff;
}


void ff_free(ff_t *ff)
{
	for (int i = 0; i < 2; i++)
		free(ff->fields[i]);

	free(ff);
}


static inline v3f_t ff_sample(v3f_t *field, size_t size, v3f_t *min, v3f_t *max, v3f_t *t)
{
	v3f_t	*a, *b, *c, *d, *e, *f, *g, *h;
	size_t	ss = size * size;

	a = &field[(size_t)min->x * ss + (size_t)max->y * size + (size_t)min->z];
	b = &field[(size_t)max->x * ss + (size_t)max->y * size + (size_t)min->z];
	c = &field[(size_t)min->x * ss + (size_t)min->y * size + (size_t)min->z];
	d = &field[(size_t)max->x * ss + (size_t)min->y * size + (size_t)min->z];
	e = &field[(size_t)min->x * ss + (size_t)max->y * size + (size_t)max->z];
	f = &field[(size_t)max->x * ss + (size_t)max->y * size + (size_t)max->z];
	g = &field[(size_t)min->x * ss + (size_t)min->y * size + (size_t)max->z];
	h = &field[(size_t)max->x * ss + (size_t)min->y * size + (size_t)max->z];

	return v3f_trilerp(a, b, c, d, e, f, g, h, t);
}


/* return an interpolated value from ff for the supplied coordinate */
/* coordinate must be in the range 0-1,0-1,0-1 */
/* w must be in the range 0-1 and determines how much of field 0 or 1 contributes to the result */
v3f_t ff_get(ff_t *ff, v3f_t *coordinate, float w)
{
	v3f_t		scaled, min, max, t, A, B;

	assert(w <= 1.f && w >= 0.f);
	assert(coordinate->x <= 1.f && coordinate->x >= 0.f);
	assert(coordinate->y <= 1.f && coordinate->y >= 0.f);
	assert(coordinate->z <= 1.f && coordinate->z >= 0.f);

	scaled = v3f_mult_scalar(coordinate, ff->size - 1);

	/* define the cube flanking the requested coordinate */
	min.x = floorf(scaled.x - 0.5f) + 0.5f;
	min.y = floorf(scaled.y - 0.5f) + 0.5f;
	min.z = floorf(scaled.z - 0.5f) + 0.5f;

	max.x = min.x + 1.0f;
	max.y = min.y + 1.0f;
	max.z = min.z + 1.0f;

	t.x = scaled.x - min.x;
	t.y = scaled.y - min.y;
	t.z = scaled.z - min.z;

	assert((size_t)min.x < ff->size);
	assert((size_t)min.x < ff->size);
	assert((size_t)min.y < ff->size);
	assert((size_t)max.x < ff->size);
	assert((size_t)max.x < ff->size);
	assert((size_t)max.y < ff->size);

	A = ff_sample(ff->fields[0], ff->size, &min, &max, &t);
	B = ff_sample(ff->fields[1], ff->size, &min, &max, &t);

	return v3f_nlerp(&A, &B, w);
}
