#include <assert.h>
#include <math.h>
#include <pthread.h>

#include "sig.h"


typedef struct ops_sin_ctxt_t {
	sig_sig_t	*hz;
	float		theta;
	unsigned	last_ticks_ms;
	pthread_mutex_t	mutex;
} ops_sin_ctxt_t;


static size_t ops_sin_size(va_list ap)
{
	return sizeof(ops_sin_ctxt_t);
}


static void ops_sin_init(void *context, va_list ap)
{
	ops_sin_ctxt_t	*ctxt = context;

	assert(ctxt);

	ctxt->hz = va_arg(ap, sig_sig_t *);
	pthread_mutex_init(&ctxt->mutex, NULL);
}


static void ops_sin_destroy(void *context)
{
	ops_sin_ctxt_t	*ctxt = context;

	assert(ctxt);

	sig_free(ctxt->hz);
	pthread_mutex_destroy(&ctxt->mutex);
}


static float output_sin(float rads)
{
	return sinf(rads) * .5f + .5f;
}


static float output_sqr(float rads)
{
	if (sinf(rads) < 0.f)
		return 0.f;

	return 1.f;
}


static float output_tri(float rads)
{
	/* This approximation comes from:
	 * https://calculushowto.com/triangle-wave-function/
	 */
	return M_2_PI * asinf(fabsf(sinf(M_PI * rads)));
}


static float output(void *context, unsigned ticks_ms, float (*output_fn)(float rads))
{
	ops_sin_ctxt_t	*ctxt = context;
	float		rads_per_ms, theta, hz;
	int		delta_ticks;

	assert(ctxt);
	assert(ctxt->hz);

	hz = sig_output(ctxt->hz, ticks_ms);
	if (hz < .001f)
		return 0.f;

	pthread_mutex_lock(&ctxt->mutex);
	/* TODO: ^^^ eliminate the need for this mutex!
	 *
	 * This became necessary when ops_sin became stateful with the
	 * addition of {last_ticks,theta}.  The experimental signals
	 * module exercising this code performs parallel rendering of
	 * signals, some of which share sig contexts via sig_ref().
	 *
	 * Prior to {last_ticks,theta}, all the sigs stuff was
	 * stateless and this shared contexts w/parallel output
	 * situation Just Worked.  So why can't we just keep things
	 * stateless like before, why have {last_ticks,theta}, what
	 * was wrong?
	 *
	 * The old ops_sin would simply multiply the incoming ticks_ms
	 * by rads_per_ms to calculate theta for the sin function.
	 * This approach produces potential discontinuities in the
	 * output when hz varies, by applying the hz to *all* of
	 * ticks_ms, and not just the delta since the last sample.  By
	 * making theta stateful, and keeping track of the last
	 * sample's ticks_ms, the current hz only gets applied to the
	 * time difference, and applied relative to the last theta,
	 * resulting in better continuity in the face of a varying hz.
	 *
	 * I'm sure this all needs more work in general...
	 */
	{
		int		ticks, last_ticks;
		unsigned	base;

		base = ticks_ms < ctxt->last_ticks_ms ? ticks_ms : ctxt->last_ticks_ms;
		ticks = ticks_ms - base;
		last_ticks = ctxt->last_ticks_ms - base;

		delta_ticks = ticks - last_ticks;
	}

	theta = ctxt->theta;
	rads_per_ms = (M_PI * 2.f) * hz * .001f;
	theta += delta_ticks * rads_per_ms;
	theta = fmodf(theta, M_PI * 2.f);

	ctxt->theta = theta;
	ctxt->last_ticks_ms = ticks_ms;
	pthread_mutex_unlock(&ctxt->mutex);

	return output_fn(theta);
}


static float ops_sin_output(void *context, unsigned ticks_ms)
{
	return output(context, ticks_ms, output_sin);
}


static float ops_sqr_output(void *context, unsigned ticks_ms)
{
	return output(context, ticks_ms, output_sqr);
}


static float ops_tri_output(void *context, unsigned ticks_ms)
{
	return output(context, ticks_ms, output_tri);
}


sig_ops_t	sig_ops_sin = {
	.size = ops_sin_size,
	.init = ops_sin_init,
	.destroy = ops_sin_destroy,
	.output = ops_sin_output,
};


sig_ops_t	sig_ops_sqr = {
	.size = ops_sin_size,
	.init = ops_sin_init,
	.destroy = ops_sin_destroy,
	.output = ops_sqr_output,
};


sig_ops_t	sig_ops_tri = {
	.size = ops_sin_size,
	.init = ops_sin_init,
	.destroy = ops_sin_destroy,
	.output = ops_tri_output,
};
