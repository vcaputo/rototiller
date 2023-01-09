#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "til_tap.h"

/* A "tap" is a named binding of a local variable+pointer to that variable.
 *
 * It's purpose is to facilitate exposing local variables controlling rendering
 * to potential external influence.
 *
 * While the tap alone does get named, this is mostly for ergonomic reasons since
 * it's convenient and natural to name the tap while specifying its variable and
 * pointer by name as well.  Putting them all in once place.
 *
 * The tap itself is not a registry or otherwise discoverable entity by itself.
 * This is strictly just the local glue, with a name.  Other pieces must tie taps
 * into streams or settings stuff for addressing them by name at a path or other
 * means.
 *
 * Note the intended way for taps to work is that the caller will always access their
 * local variables indirectly via the pointers they provided when creating the taps.
 * There will be a function for managing the tap the caller must call before accessing
 * the variable indirectly as well.  It's that function which will update the indirection
 * pointer to potentially point elsewhere if another tap is driving the variable.
 */

struct til_tap_t {
	til_tap_type_t	type;
	void		*ptr;		/* points at the caller-provided tap-managed indirection pointer */
	size_t		n_elems;	/* when > 1, *ptr is an array of n_elems elements.  Otherwise individual variable. */
	void		*elems;		/* points at the first element of type type, may or may not be an array of them */
	char		name[];
};


/* This is the raw tap creator but use the type-checked wrappers in the header and add one if one's missing */
til_tap_t * til_tap_new(til_tap_type_t type, void *ptr, const char *name, size_t n_elems, void *elems)
{
	til_tap_t	*tap;

	assert(name);
	assert(type < TIL_TAP_TYPE_MAX);
	assert(ptr);
	assert(n_elems);
	assert(elems);

	tap = calloc(1, sizeof(til_tap_t) + strlen(name) + 1);
	if (!tap)
		return NULL;

	strcpy(tap->name, name);
	tap->type = type;
	tap->ptr = ptr;
	tap->n_elems = n_elems;
	tap->elems = elems;

	*((void **)tap->ptr) = elems;

	return tap;
}


til_tap_t * til_tap_free(til_tap_t *tap)
{
	free(tap);

	return NULL;
}
