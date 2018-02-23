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

setting_desc_t * setting_desc_new(const char *name, const char *key, const char *regex, const char *preferred, const char *values[], const char *annotations[]);
void setting_desc_free(setting_desc_t *desc);

#endif