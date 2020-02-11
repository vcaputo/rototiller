#ifndef _SIG_H
#define _SIG_H

#include <stdarg.h>
#include <stddef.h>

typedef struct sig_t sig_t;

typedef struct sig_ops_t {
	size_t	(*size)(va_list ap);				/* return size of space needed for context for given ap */
	void	(*init)(void *context, va_list ap);		/* initialize context w/given ap */
	void	(*destroy)(void *context);			/* destroy initialized context */
	float	(*output)(void *context, unsigned ticks_ms);	/* output a value 0-1 from context appropriate @ time ticks_ms */
} sig_ops_t;

sig_t * sig_new(const sig_ops_t *ops, ...);
sig_t * sig_ref(sig_t *sig);
sig_t * sig_free(sig_t *sig);
float sig_output(sig_t *sig, unsigned ticks_ms);

sig_t * sig_new_abs(sig_t *x);
sig_t * sig_new_add(sig_t *a, sig_t *b);
sig_t * sig_new_ceil(sig_t *x);
sig_t * sig_new_clamp(sig_t *x, sig_t *min, sig_t *max);
sig_t * sig_new_const(float x);
sig_t * sig_new_div(sig_t *a, sig_t *b);
sig_t * sig_new_expand(sig_t *x);
sig_t * sig_new_floor(sig_t *x);
sig_t * sig_new_inv(sig_t *x);
sig_t * sig_new_lerp(sig_t *a, sig_t *b, sig_t *t);
sig_t * sig_new_max(sig_t *a, sig_t *b);
sig_t * sig_new_min(sig_t *a, sig_t *b);
sig_t * sig_new_mult(sig_t *a, sig_t *b);
sig_t * sig_new_neg(sig_t *x);
sig_t * sig_new_pow(sig_t *x, sig_t *y);
sig_t * sig_new_rand(void);
sig_t * sig_new_round(sig_t *x);
sig_t * sig_new_scale(sig_t *x, sig_t *min, sig_t *max);
sig_t * sig_new_sin(sig_t *hz);
sig_t * sig_new_sqr(sig_t *hz);
sig_t * sig_new_tri(sig_t *hz);
sig_t * sig_new_sub(sig_t *a, sig_t *b);

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
