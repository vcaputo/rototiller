#include <assert.h>
#include <float.h>

#include "sig.h"


typedef struct ops_div_ctxt_t {
	sig_t	*a, *b;
} ops_div_ctxt_t;


static size_t ops_div_size(va_list ap)
{
	return sizeof(ops_div_ctxt_t);
}


static void ops_div_init(void *context, va_list ap)
{
	ops_div_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->a = va_arg(ap, sig_t *);
	ctxt->b = va_arg(ap, sig_t *);
}


static void ops_div_destroy(void *context)
{
	ops_div_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->a);
	sig_free(ctxt->b);
}


static float ops_div_output(void *context, unsigned ticks_ms)
{
	ops_div_ctxt_t	*ctxt = context;
	float		divisor;

	assert(ctxt);

	/* XXX: protect against divide by zero by replacing with epsilon */
	divisor = sig_output(ctxt->b, ticks_ms);
	if (divisor == 0.f)
		divisor = FLT_EPSILON;

	return sig_output(ctxt->a, ticks_ms) / divisor;
}


sig_ops_t	sig_ops_div = {
	.size = ops_div_size,
	.init = ops_div_init,
	.destroy = ops_div_destroy,
	.output = ops_div_output,
};
