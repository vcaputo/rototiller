#include <assert.h>

#include "sig.h"


typedef struct ops_inv_ctxt_t {
	sig_sig_t	*x;
} ops_inv_ctxt_t;


static size_t ops_inv_size(va_list ap)
{
	return sizeof(ops_inv_ctxt_t);
}


/* inverts 0-1 inputs to 1-0,
 * assumes inputs will stay within 0-1 range
 */
static void ops_inv_init(void *context, va_list ap)
{
	ops_inv_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->x = va_arg(ap, sig_sig_t *);
}


static void ops_inv_destroy(void *context)
{
	ops_inv_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->x);
}


static float ops_inv_output(void *context, unsigned ticks_ms)
{
	ops_inv_ctxt_t	*ctxt = context;

	assert(ctxt);

	return 1.f - sig_output(ctxt->x, ticks_ms);
}


sig_ops_t	sig_ops_inv = {
	.size = ops_inv_size,
	.init = ops_inv_init,
	.destroy = ops_inv_destroy,
	.output = ops_inv_output,
};
