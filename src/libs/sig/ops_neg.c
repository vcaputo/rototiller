#include <assert.h>

#include "sig.h"


typedef struct ops_neg_ctxt_t {
	sig_sig_t	*x;
} ops_neg_ctxt_t;


static size_t ops_neg_size(va_list ap)
{
	return sizeof(ops_neg_ctxt_t);
}


static void ops_neg_init(void *context, va_list ap)
{
	ops_neg_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->x = va_arg(ap, sig_sig_t *);
}


static void ops_neg_destroy(void *context)
{
	ops_neg_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->x);
}


static float ops_neg_output(void *context, unsigned ticks_ms)
{
	ops_neg_ctxt_t	*ctxt = context;

	assert(ctxt);

	return -sig_output(ctxt->x, ticks_ms);
}


sig_ops_t	sig_ops_neg = {
	.size = ops_neg_size,
	.init = ops_neg_init,
	.destroy = ops_neg_destroy,
	.output = ops_neg_output,
};
