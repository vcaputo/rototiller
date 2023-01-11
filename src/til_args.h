#ifndef _TIL_ARGS_H
#define _TIL_ARGS_H

#include <stdio.h>

typedef struct til_args_t {
	const char	*module;
	const char	*video;
	const char	*seed;

	unsigned	use_defaults:1;
	unsigned	help:1;
	unsigned	gogogo:1;
	unsigned	print_pipes:1;
} til_args_t;

int til_args_parse(int argc, const char *argv[], til_args_t *res_args);
int til_args_pruned_parse(int argc, const char *argv[], til_args_t *res_args, int *res_argc, const char **res_argv[]);
int til_args_help(FILE *out);

#endif
