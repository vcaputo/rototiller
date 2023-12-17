#ifndef _TIL_UTIL_H
#define _TIL_UTIL_H

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

/* Though I don't bother including sys/param.h, some toolchains pull it in indirectly,
 * or just define MIN/MAX elsewhere.  So I'm just doing the stupid thing here and accepting
 * a preexisting MIN/MAX, assuming it's semantically identical.
 */
#ifndef MIN
#define MIN(_a, _b) \
	((_a) < (_b) ? (_a) : (_b))
#endif

#ifndef MAX
#define MAX(_a, _b) \
	((_a) > (_b) ? (_a) : (_b))
#endif

unsigned til_get_ncpus(void);

static inline float til_ticks_to_rads(unsigned ticks)
{
	return (ticks % 6283) * .001f;
}

#endif /* _TIL_UTIL_H */
