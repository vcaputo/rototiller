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

	assert(ctxt);
	assert(ctxt->hz);

	hz = sig_output(ctxt->hz, ticks_ms);
	if (hz < .001f)
		return 0.f;

	/* TODO FIXME: wrap ticks_ms into the current cycle
	 * so rads can be as small as possible for precision reasons.
	 * I had some code here which attempted it but the results were
	 * clearly broken, so it's removed for now.  As ticks_ms grows,
	 * the precision here will suffer.
	 */
	rads_per_ms = (M_PI * 2.f) * hz * .001f;
	rads = (float)ticks_ms * rads_per_ms;

	return sinf(rads) * .5f + .5f;
}


sig_ops_t	sig_ops_sin = {
	.size = ops_sin_size,
	.init = ops_sin_init,
	.destroy = ops_sin_destroy,
	.output = ops_sin_output,
};
