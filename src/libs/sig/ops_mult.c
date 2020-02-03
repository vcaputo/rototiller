#include <assert.h>

#include "sig.h"


typedef struct ops_mult_ctxt_t {
	sig_t	*a, *b;
} ops_mult_ctxt_t;


static size_t ops_mult_size(va_list ap)
{
	return sizeof(ops_mult_ctxt_t);
}


/* supply two sig_t's to be multiplied, this sig_t takes
 * ownership of them so they'll be freed by the multiplier
 * on destroy when that sig_t is freed.
 */
static void ops_mult_init(void *context, va_list ap)
{
	ops_mult_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->a = va_arg(ap, sig_t *);
	ctxt->b = va_arg(ap, sig_t *);
}


static float ops_mult_output(void *context, unsigned ticks_ms)
{
	ops_mult_ctxt_t	*ctxt = context;

	assert(ctxt);

	return sig_output(ctxt->a, ticks_ms) * sig_output(ctxt->b, ticks_ms);
}


static void ops_mult_destroy(void *context)
{
	ops_mult_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->a);
	sig_free(ctxt->b);
}


sig_ops_t	sig_ops_mult = {
	.size = ops_mult_size,
	.init = ops_mult_init,
	.destroy = ops_mult_destroy,
	.output = ops_mult_output,
};
