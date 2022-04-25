#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "til_setup.h"


/* Allocate and initialize a new til_setup_t of size bytes.
 * free_func is assigned to til_setup_t.free, and will be used for freeing the
 * instance returned when destroyed.
 *
 * Note this returns void * despite creating a til_setup_t, this is for convenience
 * as the callers are generally using it in place of calloc(), and assign it to a
 * container struct of some other type but having an embedded til_setup_t.
 */
void * til_setup_new(size_t size, void (*free_func)(til_setup_t *setup))
{
	til_setup_t	*setup;

	assert(size >= sizeof(til_setup_t));
	assert(free_func);

	setup = calloc(1, size);
	if (!setup)
		return NULL;

	setup->free = free_func;

	return setup;
}


/* Free the setup when non-NULL, using setup->free if non-NULL.
 * Always returns NULL for uses like foo = til_setup_free(foo);
 */
void * til_setup_free(til_setup_t *setup)
{
	if (!setup)
		return NULL;

	if (setup->free)
		setup->free(setup);

	return NULL;
}
