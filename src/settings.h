#ifndef _SETTINGS_H
#define _SETTINGS_H

#include <stdio.h>

/* Individual setting description */
typedef struct setting_desc_t {
	const char	*name;		/* long-form/human name for setting */
	const char	*key;		/* short-form/key for setting, used as left side of =value in settings string */
	const char	*regex;		/* value must conform to this regex */
	const char	*preferred;	/* if there's a default, this is it */
	const char	**values;	/* if a set of values is provided, listed here */
	const char	**annotations;	/* if a set of values is provided, annotations for those values may be listed here */
	char *		(*random)(void);/* if set, returns a valid random value for this setting */
} setting_desc_t;

/* For conveniently representing setting description generators */
typedef struct setting_desc_generator_t {
	const char	*key;		/* key this generator applies to */
	const char	**value_ptr;	/* where to put the value */
	setting_desc_t *(*func)(void *setup_context);
} setting_desc_generator_t;

typedef struct settings_t settings_t;

settings_t * settings_new(const char *settings);
void settings_free(settings_t *settings);
const char * settings_get_value(const settings_t *settings, const char *key);
const char * settings_get_key(const settings_t *settings, unsigned pos);
int settings_add_value(settings_t *settings, const char *key, const char *value);
char * settings_as_arg(const settings_t *settings);
int settings_apply_desc_generators(const settings_t *settings, const setting_desc_generator_t generators[], unsigned n_generators, void *setup_context, setting_desc_t **next_setting);

int setting_desc_clone(const setting_desc_t *desc, setting_desc_t **res_desc);
void setting_desc_free(setting_desc_t *desc);
int setting_desc_check(const setting_desc_t *desc, const char *value);

#ifndef SETTINGS_STR
#define _SETTINGS_STR(s)	#s
#define SETTINGS_STR(s)		_SETTINGS_STR(s)
#endif

#endif
