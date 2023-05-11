#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "til_setup.h"


/* Allocate and initialize a new til_setup_t of size bytes.
 * free_func is assigned to til_setup_t.free, and will be used for freeing the
 * instance returned when destroyed.  If free_func is NULL, free() will be
 * used by default.
 *
 * Note this returns void * despite creating a til_setup_t, this is for convenience
 * as the callers are generally using it in place of calloc(), and assign it to a
 * container struct of some other type but having an embedded til_setup_t.
 */
void * til_setup_new(size_t size, void (*free_func)(til_setup_t *setup))
{
	til_setup_t	*setup;

	assert(size >= sizeof(til_setup_t));

	setup = calloc(1, size);
	if (!setup)
		return NULL;

	setup->refcount = 1;
	setup->free = free_func;

	return setup;
}


/* bump refcount on setup */
void * til_setup_ref(til_setup_t *setup)
{
	assert(setup);

	setup->refcount++;

	return setup;
}


/* unref setup, freeing it when refcount reaches zero.
 * returns NULL if setup is freed (including when NULL was supplied for setup)
 * resturns setup when setup persists.
 * the public api is to just use til_setup_free() and discard that information,
 * but this is kept here as distinct for potential debugging purposes.
 */
static void * til_setup_unref(til_setup_t *setup)
{
	if (!setup)
		return NULL;

	assert(setup->refcount > 0);

	setup->refcount--;
	if (!setup->refcount) {
		if (setup->free)
			setup->free(setup);
		else
			free(setup);

		return NULL;
	}

	return setup;
}


/* like til_setup_unref() except always returns NULL so you
 * can't tell if it was actually freed or not, but this is sometimes
 * a convenient free-style wrapper if you have to NULL-assign a placeholder.
 */
void * til_setup_free(til_setup_t *setup)
{
	(void) til_setup_unref(setup);

	return NULL;
}
