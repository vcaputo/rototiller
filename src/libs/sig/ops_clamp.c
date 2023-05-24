#include <assert.h>

#include "sig.h"


typedef struct ops_clamp_ctxt_t {
	sig_sig_t	*value, *min, *max;
} ops_clamp_ctxt_t;


static size_t ops_clamp_size(va_list ap)
{
	return sizeof(ops_clamp_ctxt_t);
}


static void ops_clamp_init(void *context, va_list ap)
{
	ops_clamp_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->value = va_arg(ap, sig_sig_t *);
	ctxt->min = va_arg(ap, sig_sig_t *);
	ctxt->max = va_arg(ap, sig_sig_t *);
}


static void ops_clamp_destroy(void *context)
{
	ops_clamp_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->value);
	sig_free(ctxt->min);
	sig_free(ctxt->max);
}


static float ops_clamp_output(void *context, unsigned ticks_ms)
{
	ops_clamp_ctxt_t	*ctxt = context;
	float			value, min, max;

	assert(ctxt);

	value = sig_output(ctxt->value, ticks_ms);
	min = sig_output(ctxt->min, ticks_ms);
	max = sig_output(ctxt->max, ticks_ms);

	if (value < min)
		return min;

	if (value > max)
		return max;

	return value;
}


sig_ops_t	sig_ops_clamp = {
	.size = ops_clamp_size,
	.init = ops_clamp_init,
	.destroy = ops_clamp_destroy,
	.output = ops_clamp_output,
};
