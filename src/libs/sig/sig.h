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
sig_t * sig_free(sig_t *sig);
float sig_output(sig_t *sig, unsigned ticks_ms);

extern sig_ops_t	sig_ops_const;
extern sig_ops_t	sig_ops_rand;
extern sig_ops_t	sig_ops_sin;

/* TODO:
extern sig_ops_t	sig_ops_tri;
extern sig_ops_t	sig_ops_saw;
extern sig_ops_t	sig_ops_sqr;
*/

extern sig_ops_t	sig_ops_lerp;
extern sig_ops_t	sig_ops_mult;

#endif
