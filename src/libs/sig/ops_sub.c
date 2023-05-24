#include <assert.h>

#include "sig.h"


typedef struct ops_sub_ctxt_t {
	sig_sig_t	*a, *b;
} ops_sub_ctxt_t;


static size_t ops_sub_size(va_list ap)
{
	return sizeof(ops_sub_ctxt_t);
}


static void ops_sub_init(void *context, va_list ap)
{
	ops_sub_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->a = va_arg(ap, sig_sig_t *);
	ctxt->b = va_arg(ap, sig_sig_t *);
}


static void ops_sub_destroy(void *context)
{
	ops_sub_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->a);
	sig_free(ctxt->b);
}


static float ops_sub_output(void *context, unsigned ticks_ms)
{
	ops_sub_ctxt_t	*ctxt = context;

	assert(ctxt);

	return sig_output(ctxt->a, ticks_ms) - sig_output(ctxt->b, ticks_ms);
}


sig_ops_t	sig_ops_sub = {
	.size = ops_sub_size,
	.init = ops_sub_init,
	.destroy = ops_sub_destroy,
	.output = ops_sub_output,
};
