#include <assert.h>
#include <math.h>

#include "sig.h"


typedef struct ops_pow_ctxt_t {
	sig_sig_t	*x, *y;
} ops_pow_ctxt_t;


static size_t ops_pow_size(va_list ap)
{
	return sizeof(ops_pow_ctxt_t);
}


static void ops_pow_init(void *context, va_list ap)
{
	ops_pow_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->x = va_arg(ap, sig_sig_t *);
	ctxt->y = va_arg(ap, sig_sig_t *);
}


static void ops_pow_destroy(void *context)
{
	ops_pow_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->x);
	sig_free(ctxt->y);
}


static float ops_pow_output(void *context, unsigned ticks_ms)
{
	ops_pow_ctxt_t	*ctxt = context;

	assert(ctxt);

	return powf(sig_output(ctxt->x, ticks_ms), sig_output(ctxt->y, ticks_ms));
}


sig_ops_t	sig_ops_pow = {
	.size = ops_pow_size,
	.init = ops_pow_init,
	.destroy = ops_pow_destroy,
	.output = ops_pow_output,
};
