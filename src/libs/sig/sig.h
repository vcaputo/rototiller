#ifndef _SIG_H
#define _SIG_H

#include <stdarg.h>
#include <stddef.h>

typedef struct sig_sig_t sig_sig_t;

typedef struct sig_ops_t {
	size_t	(*size)(va_list ap);				/* return size of space needed for context for given ap */
	void	(*init)(void *context, va_list ap);		/* initialize context w/given ap */
	void	(*destroy)(void *context);			/* destroy initialized context */
	float	(*output)(void *context, unsigned ticks_ms);	/* output a value 0-1 from context appropriate @ time ticks_ms */
} sig_ops_t;

sig_sig_t * sig_new(const sig_ops_t *ops, ...);
sig_sig_t * sig_ref(sig_sig_t *sig);
sig_sig_t * sig_free(sig_sig_t *sig);
float sig_output(sig_sig_t *sig, unsigned ticks_ms);

sig_sig_t * sig_new_abs(sig_sig_t *x);
sig_sig_t * sig_new_add(sig_sig_t *a, sig_sig_t *b);
sig_sig_t * sig_new_ceil(sig_sig_t *x);
sig_sig_t * sig_new_clamp(sig_sig_t *x, sig_sig_t *min, sig_sig_t *max);
sig_sig_t * sig_new_const(float x);
sig_sig_t * sig_new_div(sig_sig_t *a, sig_sig_t *b);
sig_sig_t * sig_new_expand(sig_sig_t *x);
sig_sig_t * sig_new_floor(sig_sig_t *x);
sig_sig_t * sig_new_inv(sig_sig_t *x);
sig_sig_t * sig_new_lerp(sig_sig_t *a, sig_sig_t *b, sig_sig_t *t);
sig_sig_t * sig_new_max(sig_sig_t *a, sig_sig_t *b);
sig_sig_t * sig_new_min(sig_sig_t *a, sig_sig_t *b);
sig_sig_t * sig_new_mult(sig_sig_t *a, sig_sig_t *b);
sig_sig_t * sig_new_neg(sig_sig_t *x);
sig_sig_t * sig_new_pow(sig_sig_t *x, sig_sig_t *y);
sig_sig_t * sig_new_rand(void);
sig_sig_t * sig_new_round(sig_sig_t *x);
sig_sig_t * sig_new_scale(sig_sig_t *x, sig_sig_t *min, sig_sig_t *max);
sig_sig_t * sig_new_sin(sig_sig_t *hz);
sig_sig_t * sig_new_sqr(sig_sig_t *hz);
sig_sig_t * sig_new_tri(sig_sig_t *hz);
sig_sig_t * sig_new_sub(sig_sig_t *a, sig_sig_t *b);

extern sig_ops_t	sig_ops_const;
extern sig_ops_t	sig_ops_rand;
extern sig_ops_t	sig_ops_sin;
extern sig_ops_t	sig_ops_sqr;
extern sig_ops_t	sig_ops_tri;

/* TODO:
extern sig_ops_t	sig_ops_saw;
*/

extern sig_ops_t	sig_ops_abs;
extern sig_ops_t	sig_ops_add;
extern sig_ops_t	sig_ops_ceil;
extern sig_ops_t	sig_ops_clamp;
extern sig_ops_t	sig_ops_div;
extern sig_ops_t	sig_ops_expand;
extern sig_ops_t	sig_ops_floor;
extern sig_ops_t	sig_ops_inv;
extern sig_ops_t	sig_ops_lerp;
extern sig_ops_t	sig_ops_max;
extern sig_ops_t	sig_ops_min;
extern sig_ops_t	sig_ops_mult;
extern sig_ops_t	sig_ops_neg;
extern sig_ops_t	sig_ops_pow;
extern sig_ops_t	sig_ops_round;
extern sig_ops_t	sig_ops_scale;
extern sig_ops_t	sig_ops_sub;

#endif
