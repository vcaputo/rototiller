#ifndef _TIL_MODULE_CONTEXT_H
#define _TIL_MODULE_CONTEXT_H

typedef struct til_module_context_t til_module_context_t;
typedef struct til_module_t til_module_t;
typedef struct til_setup_t til_setup_t;
typedef struct til_stream_t til_stream_t;

struct til_module_context_t {
	const til_module_t	*module;
	til_stream_t		*stream; /* optional stream this context is part of (module_contexts are discoverable @setup->path when part of a stream) */
	unsigned		seed;
	unsigned		last_ticks; /* this gets updated after every render finishes, starts as the ticks supplied @ context create */
	unsigned		n_cpus;
	til_setup_t		*setup; /* Baked setup this context was made from, reffed by context.
					 * Always present as it provides the path, which is generally derived from a settings instance.
					 */

	unsigned                refcount;
};

void * til_module_context_new(const til_module_t *module, size_t size, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup);
void * til_module_context_ref(til_module_context_t *module_context);
void * til_module_context_free(til_module_context_t *module_context);

#endif
