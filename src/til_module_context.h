#ifndef _TIL_MODULE_CONTEXT_H
#define _TIL_MODULE_CONTEXT_H

#include <stdint.h>

typedef struct til_module_context_t til_module_context_t;
typedef struct til_module_t til_module_t;
typedef struct til_stream_t til_stream_t;

struct til_module_context_t {
	const til_module_t	*module;
	til_stream_t		*stream; /* optional stream this context is part of */
	unsigned		seed;
	unsigned		ticks;
	unsigned		n_cpus;
	char			*path;	/* for locating this instance of the module, NOT a file path */
	uint32_t		path_hash;
};

void * til_module_context_new(const til_module_t *module, size_t size, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path);
void * til_module_context_free(til_module_context_t *module_context);

#endif
