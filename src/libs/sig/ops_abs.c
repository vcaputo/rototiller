#include <assert.h>
#include <math.h>

#include "sig.h"


typedef struct ops_abs_ctxt_t {
	sig_sig_t	*x;
} ops_abs_ctxt_t;


static size_t ops_abs_size(va_list ap)
{
	return sizeof(ops_abs_ctxt_t);
}


static void ops_abs_init(void *context, va_list ap)
{
	ops_abs_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->x = va_arg(ap, sig_sig_t *);
}


static void ops_abs_destroy(void *context)
{
	ops_abs_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->x);
}


static float ops_abs_output(void *context, unsigned ticks_ms)
{
	ops_abs_ctxt_t	*ctxt = context;

	assert(ctxt);

	return fabsf(sig_output(ctxt->x, ticks_ms));
}


sig_ops_t	sig_ops_abs = {
	.size = ops_abs_size,
	.init = ops_abs_init,
	.destroy = ops_abs_destroy,
	.output = ops_abs_output,
};
