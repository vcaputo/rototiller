#include <assert.h>
#include <math.h>

#include "sig.h"


typedef struct ops_sin_ctxt_t {
	float	hz;
} ops_sin_ctxt_t;


static size_t ops_sin_size(va_list ap)
{
	return sizeof(ops_sin_ctxt_t);
}


static void ops_sin_init(void *context, va_list ap)
{
	ops_sin_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->hz = va_arg(ap, double);
	assert(ctxt->hz >= .0001f);
}


static float ops_sin_output(void *context, unsigned ticks_ms)
{
	ops_sin_ctxt_t	*ctxt = context;
	float		rads_per_ms, rads;
	unsigned	ms_per_cycle;

	assert(ctxt);

	ms_per_cycle = ctxt->hz * 1000.f;
	rads_per_ms = (M_PI * 2.f) * ctxt->hz * .001f;
	rads = (float)(ticks_ms % ms_per_cycle) * rads_per_ms;

	return sinf(rads);
}


sig_ops_t	sig_ops_sin = {
	.size = ops_sin_size,
	.init = ops_sin_init,
	.output = ops_sin_output,
};
