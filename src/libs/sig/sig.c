#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

#include "sig.h"

/* This is to try ensure ctxt's alignment accomodates all the base type sizes,
 * it may waste some space in sig_t but since the caller supplies just a size
 * via the supplied sig_ops_t.size(), we know nothing of the alignment reqs.
 *
 * XXX: If callers start using other types like xmmintrinsics __m128, this
 * struct will have to get those added.
 */
typedef union sig_context_t {
	float		f;
	double		d;
	long double	ld;
	char		c;
	short		s;
	int		i;
	long		l;
	long long	ll;
	void		*p;
} sig_context_t;

typedef struct sig_t {
	const sig_ops_t	*ops;
	unsigned	refcount;
	sig_context_t	ctxt[];
} sig_t;


/* return a new signal generator of ops type, configured according to va_list */
sig_t * sig_new(const sig_ops_t *ops, ...)
{
	static const sig_ops_t	null_ops;
	size_t			ctxt_size = 0;
	sig_t			*sig;
	va_list			ap;

	if (!ops)
		ops = &null_ops;

	va_start(ap, ops);
	if (ops->size)
		ctxt_size = ops->size(ap);
	va_end(ap);

	sig = calloc(1, sizeof(sig_t) + ctxt_size);
	if (!sig)
		return NULL;

	va_start(ap, ops);
	if (ops->init)
		ops->init(&sig->ctxt, ap);
	va_end(ap);

	sig->ops = ops;
	sig->refcount = 1;

	return sig;
}


/* add a reference to an existing signal generator,
 * free/unref using sig_free() just like it had been returned by sig_new()
 */
sig_t * sig_ref(sig_t *sig)
{
	assert(sig);

	sig->refcount++;

	return sig;
}


/* free a signal generator, returns sig if sig still has references,
 * otherwise returns NULL
 */
sig_t * sig_free(sig_t *sig)
{
	if (sig) {
		assert(sig->refcount > 0);

		sig->refcount--;
		if (sig->refcount)
			return sig;

		if (sig->ops->destroy)
			sig->ops->destroy(&sig->ctxt);

		free(sig);
	}

	return NULL;
}


/* produce the value for time ticks_ms from the supplied signal generator,
 * the returned value should always be kept in the range 0-1.
 */
float sig_output(sig_t *sig, unsigned ticks_ms)
{
	assert(sig);
	assert(sig->ops);

	if (sig->ops->output)
		return sig->ops->output(sig->ctxt, ticks_ms);

	return 0;
}


#ifdef TESTING

#include <stdio.h>

int main(int argc, char *argv[])
{
	sig_t	*sig;

	sig = sig_new(NULL);
	printf("null output=%f\n", sig_output(sig, 0));
	sig = sig_free(sig);

	sig = sig_new(&sig_ops_rand);
	for (unsigned j = 0; j < 2; j++) {
		for (unsigned i = 0; i < 10; i++)
			printf("rand j=%u i=%u output=%f\n", j, i, sig_output(sig, i));
	}

	sig = sig_new(&sig_ops_sin, sig_new(&sig_ops_const, 2.f));
	for (unsigned i = 0; i < 1000; i++)
		printf("sin 2hz output %i=%f\n", i, sig_output(sig, i));
	sig = sig_free(sig);

	sig = sig_new(&sig_ops_mult,
		      sig_new(&sig_ops_sin, sig_new(&sig_ops_const, 1.f)),	/* LFO @ 1hz */
		      sig_new(&sig_ops_sin, sig_new(&sig_ops_const, 100.f))	/* oscillator @ 100hz */
		);
	for (unsigned i = 0; i < 1000; i++)
		printf("sin 100hz * 1hz output %i=%f\n", i, sig_output(sig, i));
	sig = sig_free(sig);

	sig =	sig_new(&sig_ops_pow,								/* raise an ... */
			sig_new(&sig_ops_sin,							/* oscillator ... */
				sig_new(&sig_ops_const, 10.f)),					/* @ 10hz, */
			sig_new(&sig_ops_round,							/* to a rounded .. */
				sig_new(&sig_ops_mult, sig_new(&sig_ops_const, 50.f),		/* 50 X ... */
					sig_new(&sig_ops_sin, sig_new(&sig_ops_const, 1.f))	/* 1hz oscillator */
				)
			)
		);
	for (unsigned i = 0; i < 1000; i++)
		printf("sin 10hz ^ (sin 1hz * 50) output %i=%f\n", i, sig_output(sig, i));
	sig = sig_free(sig);

	sig =	sig_new(&sig_ops_scale,								/* scale a */
			sig_new(&sig_ops_lerp,							/* linear interpolation */
				sig_new(&sig_ops_sin, sig_new(&sig_ops_const, 10.f)),		/* between one 10hz oscillator */
				sig_new(&sig_ops_sin, sig_new(&sig_ops_const, 33.f)),		/* and another 33hz oscillator */
				sig_new(&sig_ops_sin, sig_new(&sig_ops_const, 2.f))		/* weighted by a 2hz oscillator */
			),
			sig_new(&sig_ops_const, -100.f), sig_new(&sig_ops_const, 100.f)		/* to the range -100 .. +100 */
		);
	for (unsigned i = 0; i < 1000; i++)
		printf("scale(lerp(sin(10hz), sin(33hz), sin(2hz)), -100, +100)  output %i=%f\n", i, sig_output(sig, i));
	sig = sig_free(sig);

}

#endif
