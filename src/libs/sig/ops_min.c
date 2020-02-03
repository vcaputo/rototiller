#include <assert.h>

#include "sig.h"


typedef struct ops_min_ctxt_t {
	sig_t	*a, *b;
} ops_min_ctxt_t;


static size_t ops_min_size(va_list ap)
{
	return sizeof(ops_min_ctxt_t);
}


/* supply two sig_t's to be multiplied, this sig_t takes
 * ownership of them so they'll be freed by the multiplier
 * on destroy when that sig_t is freed.
 */
static void ops_min_init(void *context, va_list ap)
{
	ops_min_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->a = va_arg(ap, sig_t *);
	ctxt->b = va_arg(ap, sig_t *);
}


static void ops_min_destroy(void *context)
{
	ops_min_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->a);
	sig_free(ctxt->b);
}


static float ops_min_output(void *context, unsigned ticks_ms)
{
	ops_min_ctxt_t	*ctxt = context;
	float		a, b;

	assert(ctxt);

	a = sig_output(ctxt->a, ticks_ms);
	b = sig_output(ctxt->b, ticks_ms);

	return a < b ? a : b;
}


sig_ops_t	sig_ops_min = {
	.size = ops_min_size,
	.init = ops_min_init,
	.destroy = ops_min_destroy,
	.output = ops_min_output,
};
