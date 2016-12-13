#ifndef _UTIL_H
#define _UTIL_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define exit_if(_cond, _fmt, ...) \
	if (_cond) { \
		fprintf(stderr, "Fatal error: " _fmt "\n", ##__VA_ARGS__); \
		exit(EXIT_FAILURE); \
	}

#define pexit_if(_cond, _fmt, ...) \
	exit_if(_cond, _fmt ": %s", ##__VA_ARGS__, strerror(errno))

#define nelems(_array) \
	(sizeof(_array) / sizeof(_array[0]))

#define cstrlen(_str) \
	(sizeof(_str) - 1)

unsigned get_ncpus(void);
void ask_string(char *buf, int len, const char *prompt, const char *def);
void ask_num(int *res, int max, const char *prompt, int def);

#endif /* _UTIL_H */
