#include <assert.h>
#include <stdlib.h>

#include "sig.h"


static float ops_rand_output(void *context, unsigned ticks_ms)
{
	return rand_r(&ticks_ms) * 1.f / (float)RAND_MAX;
}


sig_ops_t	sig_ops_rand = {
	.output = ops_rand_output,
};

