#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "til_setup.h"


/* Allocate and initialize a new til_setup_t of size bytes.
 * free_func is assigned to til_setup_t.free, and will be used for freeing the
 * instance returned when destroyed.
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
