#include <assert.h>

#include "sig.h"


typedef struct ops_expand_ctxt_t {
	sig_t	*value;
} ops_expand_ctxt_t;


static size_t ops_expand_size(va_list ap)
{
	return sizeof(ops_expand_ctxt_t);
}


/* expects a single sig_t: value
 * input range is assumed to be 0..1, outputs expanded
 * range of -1..+1
 */
static void ops_expand_init(void *context, va_list ap)
{
	ops_expand_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->value = va_arg(ap, sig_t *);
}


static void ops_expand_destroy(void *context)
{
	ops_expand_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->value);
}


static float ops_expand_output(void *context, unsigned ticks_ms)
{
	ops_expand_ctxt_t	*ctxt = context;

	assert(ctxt);

	return sig_output(ctxt->value, ticks_ms) * 2.f + 1.f;
}


sig_ops_t	sig_ops_expand = {
	.size = ops_expand_size,
	.init = ops_expand_init,
	.destroy = ops_expand_destroy,
	.output = ops_expand_output,
};
