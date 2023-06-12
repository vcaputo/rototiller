#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "til.h"
#include "til_jenkins.h"
#include "til_module_context.h"
#include "til_setup.h"
#include "til_stream.h"


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
 *
 * setup must not be NULL, even for modules without a setup method, as the setup *always* provides the path
 * for the context.  The context takes a reference on the provided setup, which will be dropped when the
 * context is freed.
 */
void * til_module_context_new(const til_module_t *module, size_t size, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	til_module_context_t	*module_context;

	assert(module);
	assert(size >= sizeof(til_module_context_t));
	assert(n_cpus > 0);
	assert(setup); /* modules must be able to key things like taps off their context's path @ setup->path */

	module_context = calloc(1, size);
	if (!module_context)
		return NULL;

	module_context->module = module;
	module_context->stream = stream;
	module_context->seed = seed;
	module_context->ticks = ticks;
	module_context->n_cpus = n_cpus;
	module_context->setup = til_setup_ref(setup);
	module_context->refcount = 1;

	return module_context;
}


/* Free the module_context when non-NULL, using module_context->module->destroy_context if non-NULL.
 * Always returns NULL for uses like foo = til_module_context_free(foo);
 *
 * Note this replaces til_module_destroy_context(), which has been removed.
 */
static inline void * module_context_free(til_module_context_t *module_context)
{
	til_stream_t	*stream;
	til_setup_t	*setup;

	assert(module_context);
	assert(!module_context->refcount);

	stream = module_context->stream;
	setup = module_context->setup;

	if (module_context->module->destroy_context)
		module_context->module->destroy_context(module_context);
	else
		free(module_context);

	(void) til_setup_free(setup); /* free last just in case the module destructor makes use of it */

	/* cleanup any pipes this context might have had in the stream, if the
	 * module's destroy_context() also does this it's harmlessly idempotent
	 * besides wasting some cycles.  But by always doing it here, we're sure
	 * to not leave dangling references.
	 */
	til_stream_untap_owner(stream, module_context);

	return NULL;
}


/* bump refcount on context */
void * til_module_context_ref(til_module_context_t *module_context)
{
	assert(module_context);

	module_context->refcount++;

	return module_context;
}


/* unref the module_context, returns non-NULL when module_context persists */
static void * til_module_context_unref(til_module_context_t *module_context)
{
/* XXX this is kept private until there's a real use case in need of the distinct return values vs. free.
 * til_setup is the same way, which this is basically mirroring
 */
	if (!module_context)
		return NULL;

	assert(module_context->refcount > 0);

	module_context->refcount--;
	if (!module_context->refcount)
		return module_context_free(module_context);

	return module_context;
}


void * til_module_context_free(til_module_context_t *module_context)
{
	(void) til_module_context_unref(module_context);

	return NULL;
}
