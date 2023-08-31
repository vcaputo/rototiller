#ifndef _TIL_SETUP_H
#define _TIL_SETUP_H

#include <stdint.h>

typedef struct til_settings_t til_settings_t;
typedef struct til_setting_t til_setting_t;
typedef struct til_setup_t til_setup_t;

struct til_setup_t {
	const char	*path;
	uint32_t	path_hash;
	unsigned	refcount;
	void		(*free)(til_setup_t *setup);
	const void	*creator;
};

void * til_setup_new(const til_settings_t *settings, size_t size, void (*free_func)(til_setup_t *setup), const void *creator);
void * til_setup_ref(til_setup_t *setup);
void * til_setup_free(til_setup_t *setup);
int til_setup_free_with_failed_setting_ret_err(til_setup_t *setup, til_setting_t *failed_setting, til_setting_t **res_setting, int err);

#endif
