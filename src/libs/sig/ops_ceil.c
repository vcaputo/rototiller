#include <assert.h>
#include <math.h>

#include "sig.h"


typedef struct ops_ceil_ctxt_t {
	sig_sig_t	*x;
} ops_ceil_ctxt_t;


static size_t ops_ceil_size(va_list ap)
{
	return sizeof(ops_ceil_ctxt_t);
}


static void ops_ceil_init(void *context, va_list ap)
{
	ops_ceil_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->x = va_arg(ap, sig_sig_t *);
}


static void ops_ceil_destroy(void *context)
{
	ops_ceil_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->x);
}


static float ops_ceil_output(void *context, unsigned ticks_ms)
{
	ops_ceil_ctxt_t	*ctxt = context;

	assert(ctxt);

	return ceilf(sig_output(ctxt->x, ticks_ms));
}


sig_ops_t	sig_ops_ceil = {
	.size = ops_ceil_size,
	.init = ops_ceil_init,
	.destroy = ops_ceil_destroy,
	.output = ops_ceil_output,
};
