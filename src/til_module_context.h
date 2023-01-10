#ifndef _TIL_MODULE_CONTEXT_H
#define _TIL_MODULE_CONTEXT_H

typedef struct til_module_context_t til_module_context_t;
typedef struct til_module_t til_module_t;

struct til_module_context_t {
	const til_module_t	*module;
	unsigned		seed;
	unsigned		ticks;
	unsigned		n_cpus;
	char			*path;	/* for locating this instance of the module, NOT a file path */
};

void * til_module_context_new(size_t size, unsigned seed, unsigned ticks, unsigned n_cpus, char *path);
void * til_module_context_free(til_module_context_t *module_context);

#endif
