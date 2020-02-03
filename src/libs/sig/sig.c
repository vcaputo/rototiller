#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

#include "sig.h"


typedef struct sig_t {
	const sig_ops_t	*ops;
	void		*ctxt[];
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

	return sig;
}


/* free a signal generator, always returns NULL */
sig_t * sig_free(sig_t *sig)
{
	if (sig) {
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

	sig = sig_new(&sig_ops_sin, 2.f);
	for (unsigned i = 0; i < 1000; i++)
		printf("sin 2hz output %i=%f\n", i, sig_output(sig, i));
	sig = sig_free(sig);

	sig = sig_new(&sig_ops_mult,
		      sig_new(&sig_ops_sin, 1.f),	/* LFO @ 1hz */
		      sig_new(&sig_ops_sin, 100.f)	/* oscillator @ 100hz */
		);
	for (unsigned i = 0; i < 1000; i++)
		printf("sin 100hz * 1hz output %i=%f\n", i, sig_output(sig, i));
	sig = sig_free(sig);
}

#endif
