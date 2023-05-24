#include <assert.h>

#include "sig.h"


typedef struct ops_max_ctxt_t {
	sig_sig_t	*a, *b;
} ops_max_ctxt_t;


static size_t ops_max_size(va_list ap)
{
	return sizeof(ops_max_ctxt_t);
}


/* supply two sig_sig_t's to be multiplied, this sig_sig_t takes
 * ownership of them so they'll be freed by the multiplier
 * on destroy when that sig_sig_t is freed.
 */
static void ops_max_init(void *context, va_list ap)
{
	ops_max_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->a = va_arg(ap, sig_sig_t *);
	ctxt->b = va_arg(ap, sig_sig_t *);
}


static void ops_max_destroy(void *context)
{
	ops_max_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->a);
	sig_free(ctxt->b);
}


static float ops_max_output(void *context, unsigned ticks_ms)
{
	ops_max_ctxt_t	*ctxt = context;
	float		a, b;

	assert(ctxt);

	a = sig_output(ctxt->a, ticks_ms);
	b = sig_output(ctxt->b, ticks_ms);

	return a > b ? a : b;
}


sig_ops_t	sig_ops_max = {
	.size = ops_max_size,
	.init = ops_max_init,
	.destroy = ops_max_destroy,
	.output = ops_max_output,
};
