#include <assert.h>

#include "sig.h"


typedef struct ops_lerp_ctxt_t {
	sig_t	*a, *b, *t;
} ops_lerp_ctxt_t;


static size_t ops_lerp_size(va_list ap)
{
	return sizeof(ops_lerp_ctxt_t);
}


/* Supply two sig_t's to be interpolated and another for the t, this sig_t
 * takes ownership of them so they'll be freed on destroy when the ops_lerp
 * sig_t is freed.
 */
static void ops_lerp_init(void *context, va_list ap)
{
	ops_lerp_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->a = va_arg(ap, sig_t *);
	ctxt->b = va_arg(ap, sig_t *);
	ctxt->t = va_arg(ap, sig_t *);
}


static void ops_lerp_destroy(void *context)
{
	ops_lerp_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->a);
	sig_free(ctxt->b);
	sig_free(ctxt->t);
}


static inline float lerp(float a, float b, float t)
{
	return (1.f - t) * a + b * t;
}


static float ops_lerp_output(void *context, unsigned ticks_ms)
{
	ops_lerp_ctxt_t	*ctxt = context;

	assert(ctxt);

	return lerp(sig_output(ctxt->a, ticks_ms),
		    sig_output(ctxt->b, ticks_ms),
		    sig_output(ctxt->t, ticks_ms));
}


sig_ops_t	sig_ops_lerp = {
	.size = ops_lerp_size,
	.init = ops_lerp_init,
	.destroy = ops_lerp_destroy,
	.output = ops_lerp_output,
};
