#include <assert.h>
#include <math.h>

#include "sig.h"


typedef struct ops_sin_ctxt_t {
	sig_t	*hz;
} ops_sin_ctxt_t;


static size_t ops_sin_size(va_list ap)
{
	return sizeof(ops_sin_ctxt_t);
}


static void ops_sin_init(void *context, va_list ap)
{
	ops_sin_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->hz = va_arg(ap, sig_t *);
}


static void ops_sin_destroy(void *context)
{
	ops_sin_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->hz);
}


static float ops_sin_output(void *context, unsigned ticks_ms)
{
	ops_sin_ctxt_t	*ctxt = context;
	float		rads_per_ms, rads, hz;
	unsigned	ms_per_cycle;

	assert(ctxt);
	assert(ctxt->hz);

	hz = sig_output(ctxt->hz, ticks_ms);
	if (hz < .0001f)
		return 0.f;

	ms_per_cycle = hz * 1000.f;
	rads_per_ms = (M_PI * 2.f) * hz * .001f;
	rads = (float)(ticks_ms % ms_per_cycle) * rads_per_ms;

	return sinf(rads) * .5f + .5f;
}


sig_ops_t	sig_ops_sin = {
	.size = ops_sin_size,
	.init = ops_sin_init,
	.destroy = ops_sin_destroy,
	.output = ops_sin_output,
};
