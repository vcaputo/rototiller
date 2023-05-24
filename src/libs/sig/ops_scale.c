#include <assert.h>

#include "sig.h"


typedef struct ops_scale_ctxt_t {
	sig_sig_t	*value, *min, *max;
} ops_scale_ctxt_t;


static size_t ops_scale_size(va_list ap)
{
	return sizeof(ops_scale_ctxt_t);
}


/* expects three sig_sig_t's: value, min, max.
 * min is assumed to be always <= max,
 * and value is assumed to always be 0-1
 */
static void ops_scale_init(void *context, va_list ap)
{
	ops_scale_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->value = va_arg(ap, sig_sig_t *);
	ctxt->min = va_arg(ap, sig_sig_t *);
	ctxt->max = va_arg(ap, sig_sig_t *);
}


static void ops_scale_destroy(void *context)
{
	ops_scale_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->value);
	sig_free(ctxt->min);
	sig_free(ctxt->max);
}


static float ops_scale_output(void *context, unsigned ticks_ms)
{
	ops_scale_ctxt_t	*ctxt = context;
	float			value, min, max;

	assert(ctxt);

	value = sig_output(ctxt->value, ticks_ms);
	min = sig_output(ctxt->min, ticks_ms);
	max = sig_output(ctxt->max, ticks_ms);

	assert(max >= min);

	return value * (max - min) + min;
}


sig_ops_t	sig_ops_scale = {
	.size = ops_scale_size,
	.init = ops_scale_init,
	.destroy = ops_scale_destroy,
	.output = ops_scale_output,
};
