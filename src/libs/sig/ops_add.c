#include <assert.h>

#include "sig.h"


typedef struct ops_add_ctxt_t {
	sig_t	*a, *b;
} ops_add_ctxt_t;


static size_t ops_add_size(va_list ap)
{
	return sizeof(ops_add_ctxt_t);
}


static void ops_add_init(void *context, va_list ap)
{
	ops_add_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->a = va_arg(ap, sig_t *);
	ctxt->b = va_arg(ap, sig_t *);
}


static void ops_add_destroy(void *context)
{
	ops_add_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->a);
	sig_free(ctxt->b);
}


static float ops_add_output(void *context, unsigned ticks_ms)
{
	ops_add_ctxt_t	*ctxt = context;

	assert(ctxt);

	return sig_output(ctxt->a, ticks_ms) + sig_output(ctxt->b, ticks_ms);
}


sig_ops_t	sig_ops_add = {
	.size = ops_add_size,
	.init = ops_add_init,
	.destroy = ops_add_destroy,
	.output = ops_add_output,
};
