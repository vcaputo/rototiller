#include <assert.h>
#include <math.h>

#include "sig.h"


typedef struct ops_const_ctxt_t {
	float	value;
} ops_const_ctxt_t;


static size_t ops_const_size(va_list ap)
{
	return sizeof(ops_const_ctxt_t);
}


static void ops_const_init(void *context, va_list ap)
{
	ops_const_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->value = va_arg(ap, double);
}


static float ops_const_output(void *context, unsigned ticks_ms)
{
	ops_const_ctxt_t	*ctxt = context;

	assert(ctxt);

	return ctxt->value;
}


sig_ops_t	sig_ops_const = {
	.size = ops_const_size,
	.init = ops_const_init,
	.output = ops_const_output,
};

