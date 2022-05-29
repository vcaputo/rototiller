#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "til.h"
#include "til_module_context.h"


/* Allocate and initialize a new til_module_context_t of size bytes.
 * It'd be nice to assign module_context->module here as well, but since this gets called
 * almost exclusively as a helper from within modules' create_context(), it'd make the
 * frequently-written module code more annoying and verbose to do so, since their til_module_t is
 * preferably at EOF.  So in the interests of preserving that clean/ergonomic layout for modules,
 * assignment of the .module memeber occurs in the public til_module_create_context(), which seems
 * like an OK compromise.
 *
 * If the assigned module's til_module_t->destroy_context is NULL, libc's free() will be used to
 * free the context.  This should be fine as long as statically allocated contexts never become a
 * thing, which seems unlikely.  Doing it this way permits modules to omit their destroy_context()
 * altogether if it would simply free(context).
 *
 * Note this returns void * despite creating a til_module_context_t, this is for convenience
 * as the callers are generally using it in place of calloc(), and assign it to a
 * container struct of some other type but having an embedded til_module_context_t.
 */
void * til_module_context_new(size_t size, unsigned seed, unsigned n_cpus)
{
	til_module_context_t	*module_context;

	assert(size >= sizeof(til_module_context_t));
	assert(n_cpus > 0);

	module_context = calloc(1, size);
	if (!module_context)
		return NULL;

	module_context->seed = seed;
	module_context->n_cpus = n_cpus;

	return module_context;
}


/* Free the module_context when non-NULL, using module_context->module->destroy_context if non-NULL.
 * Always returns NULL for uses like foo = til_module_context_free(foo);
 *
 * Note this replaces til_module_destroy_context(), which has been removed.
 */
void * til_module_context_free(til_module_context_t *module_context)
{
	if (!module_context)
		return NULL;

	if (module_context->module->destroy_context)
		module_context->module->destroy_context(module_context);
	else
		free(module_context);

	return NULL;
}
