#ifndef _TIL_SETTINGS_H
#define _TIL_SETTINGS_H

#include <stdio.h>

/* Individual setting description */
typedef struct til_setting_desc_t {
	const char	*name;		/* long-form/human name for setting */
	const char	*key;		/* short-form/key for setting, used as left side of =value in settings string */
	const char	*regex;		/* value must conform to this regex */
	const char	*preferred;	/* if there's a default, this is it */
	const char	**values;	/* if a set of values is provided, listed here */
	const char	**annotations;	/* if a set of values is provided, annotations for those values may be listed here */
	char *		(*random)(void);/* if set, returns a valid random value for this setting */
} til_setting_desc_t;

/* For conveniently representing setting description generators */
typedef struct til_setting_desc_generator_t {
	const char	*key;		/* key this generator applies to */
	const char	**value_ptr;	/* where to put the value */
	int		(*func)(void *setup_context, til_setting_desc_t **res_desc);
} til_setting_desc_generator_t;

typedef struct til_settings_t til_settings_t;

til_settings_t * til_settings_new(const char *settings);
til_settings_t * til_settings_free(til_settings_t *settings);
const char * til_settings_get_value(const til_settings_t *settings, const char *key);
const char * til_settings_get_key(const til_settings_t *settings, unsigned pos);
int til_settings_add_value(til_settings_t *settings, const char *key, const char *value);
char * til_settings_as_arg(const til_settings_t *settings);
int til_settings_apply_desc_generators(const til_settings_t *settings, const til_setting_desc_generator_t generators[], unsigned n_generators, void *setup_context, til_setting_desc_t **next_setting);

int til_setting_desc_clone(const til_setting_desc_t *desc, til_setting_desc_t **res_desc);
til_setting_desc_t * til_setting_desc_free(til_setting_desc_t *desc);
int til_setting_desc_check(const til_setting_desc_t *desc, const char *value);

#ifndef TIL_SETTINGS_STR
#define _TIL_SETTINGS_STR(s)	#s
#define TIL_SETTINGS_STR(s)	_TIL_SETTINGS_STR(s)
#endif

#endif