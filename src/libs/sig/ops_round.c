#include <assert.h>
#include <math.h>

#include "sig.h"


typedef struct ops_round_ctxt_t {
	sig_sig_t	*x;
} ops_round_ctxt_t;


static size_t ops_round_size(va_list ap)
{
	return sizeof(ops_round_ctxt_t);
}


static void ops_round_init(void *context, va_list ap)
{
	ops_round_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->x = va_arg(ap, sig_sig_t *);
}


static void ops_round_destroy(void *context)
{
	ops_round_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->x);
}


static float ops_round_output(void *context, unsigned ticks_ms)
{
	ops_round_ctxt_t	*ctxt = context;

	assert(ctxt);

	return roundf(sig_output(ctxt->x, ticks_ms));
}


sig_ops_t	sig_ops_round = {
	.size = ops_round_size,
	.init = ops_round_init,
	.destroy = ops_round_destroy,
	.output = ops_round_output,
};
