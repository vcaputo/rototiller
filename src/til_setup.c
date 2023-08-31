#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "til_jenkins.h"
#include "til_settings.h"
#include "til_setup.h"
#include "til_str.h"


/* Allocate and initialize a new til_setup_t of size bytes.
 * free_func is assigned to til_setup_t.free, and will be used for freeing the
 * instance returned when destroyed.  If free_func is NULL, free() will be
 * used by default.
 *
 * A copy of the provided settings' path is stored at til_setup_t.path, and will
 * always be freed automatically when the setup instance is freed, independent of
 * free_func.
 *
 * Note this returns void * despite creating a til_setup_t, this is for convenience
 * as the callers are generally using it in place of calloc(), and assign it to a
 * container struct of some other type but having an embedded til_setup_t.
 */
void * til_setup_new(const til_settings_t *settings, size_t size, void (*free_func)(til_setup_t *setup), const void *creator)
{
	char		*path_buf = NULL;
	size_t		path_len;
	til_str_t	*path_str;
	til_setup_t	*setup;
	int		r;

	assert(settings);
	assert(size >= sizeof(til_setup_t));

	path_str = til_str_new("");
	if (!path_str)
		return NULL;

	r = til_settings_strprint_path(settings, path_str);
	if (r < 0)
		return til_str_free(path_str);

	path_buf = til_str_to_buf(path_str, &path_len);

	setup = calloc(1, size);
	if (!setup) {
		free(path_buf);
		return NULL;
	}

	setup->path = path_buf;
	setup->path_hash = til_jenkins((uint8_t *)path_buf, path_len + 1 /* include the \0 */);
	setup->refcount = 1;
	setup->free = free_func;
	setup->creator = creator;

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
		/* don't make setup->free() free the path when provided */
		free((void *)setup->path);

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


/* ergonomic helper for setup_funcs to use in res_setup baking */
int til_setup_free_with_failed_setting_ret_err(til_setup_t *setup, til_setting_t *failed_setting, til_setting_t **res_setting, int err)
{
	assert(failed_setting);
	assert(res_setting);
	assert(err < 0);

	til_setup_free(setup);
	*res_setting = failed_setting;

	return err;
}


/* another ergonomic helper for setup_funcs to use in res_setup baking */
int til_setup_free_with_ret_err(til_setup_t *setup, int err)
{
	assert(err < 0);

	til_setup_free(setup);

	return err;
}
