#include <assert.h>
#include <math.h>

#include "sig.h"


typedef struct ops_floor_ctxt_t {
	sig_t	*x;
} ops_floor_ctxt_t;


static size_t ops_floor_size(va_list ap)
{
	return sizeof(ops_floor_ctxt_t);
}


static void ops_floor_init(void *context, va_list ap)
{
	ops_floor_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->x = va_arg(ap, sig_t *);
}


static void ops_floor_destroy(void *context)
{
	ops_floor_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->x);
}


static float ops_floor_output(void *context, unsigned ticks_ms)
{
	ops_floor_ctxt_t	*ctxt = context;

	assert(ctxt);

	return floorf(sig_output(ctxt->x, ticks_ms));
}


sig_ops_t	sig_ops_floor = {
	.size = ops_floor_size,
	.init = ops_floor_init,
	.destroy = ops_floor_destroy,
	.output = ops_floor_output,
};
