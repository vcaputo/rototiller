#ifndef _TIL_SETTINGS_H
#define _TIL_SETTINGS_H

#include <stdio.h>

typedef struct til_setting_t til_setting_t;
typedef struct til_settings_t til_settings_t;
typedef struct til_setup_t til_setup_t;

/* Individual setting specification */
typedef struct til_setting_spec_t {
	const char	*name;		/* long-form/human name for setting */
	const char	*key;		/* short-form/key for setting, used as left side of =value in settings string */
	const char	*regex;		/* value must conform to this regex */
	const char	*preferred;	/* if there's a default, this is it */
	const char	**values;	/* if a set of values is provided, listed here */
	const char	**annotations;	/* if a set of values is provided, annotations for those values may be listed here */
	char *		(*random)(unsigned seed);/* if set, returns a valid random value for this setting */
	unsigned	as_nested_settings:1;	/* if set, this setting expects a settings string for its value and wants a til_setting_t.value_as_nested_settings instance created for it */
	unsigned	as_label:1;	/* if set, this setting's value is to be used as a label component in path construction - only applies to the first setting entry in an instance (til_settings_t.entries[0]) */
} til_setting_spec_t;

/* a setting_desc is a setting_spec that's been described to a specific containing settings instance via desc_new(), though desc_new() takes a const settings it's cast away when placed into the allocated desc. */
typedef struct til_setting_desc_t {
	til_settings_t		*container;
	til_setting_spec_t	spec;
} til_setting_desc_t;

/* For conveniently representing setting description generators */
typedef struct til_setting_desc_generator_t {
	const char	*key;		/* key this generator applies to */
	const char	**value_ptr;	/* where to put the value */
	int		(*func)(const til_settings_t *settings, til_setup_t *setup_context, const til_setting_desc_t **res_desc);
} til_setting_desc_generator_t;

/* Encapsulates a single til_settings_t.entries[] entry */
struct til_setting_t {
	til_settings_t			*parent;
	til_settings_t			*value_as_nested_settings;	/* XXX: non-NULL when setup turned this setting's value into a nested settings instance */
	const char			*key;
	const char			*value;
	const til_setting_desc_t	*desc;
	void				*user_data;
};

til_settings_t * til_settings_new(const til_settings_t *parent, const char *label, const char *settings);
til_settings_t * til_settings_free(til_settings_t *settings);
unsigned til_settings_get_count(const til_settings_t *settings);
const char * til_settings_get_value_by_key(const til_settings_t *settings, const char *key, til_setting_t **res_setting);
const char * til_settings_get_value_by_idx(const til_settings_t *settings, unsigned idx, til_setting_t **res_setting);
til_setting_t * til_settings_add_value(til_settings_t *settings, const char *key, const char *value, const til_setting_desc_t *desc);
void til_settings_reset_descs(til_settings_t *settings);
int til_settings_get_and_describe_value(const til_settings_t *settings, const til_setting_spec_t *spec, const char **res_value, til_setting_t **res_setting, const til_setting_desc_t **res_desc);
char * til_settings_as_arg(const til_settings_t *settings);
int til_settings_apply_desc_generators(const til_settings_t *settings, const til_setting_desc_generator_t generators[], unsigned n_generators, til_setup_t *setup, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);

int til_setting_desc_new(const til_settings_t *settings, const til_setting_spec_t *spec, const til_setting_desc_t **res_desc);
til_setting_desc_t * til_setting_desc_free(const til_setting_desc_t *desc);
int til_setting_desc_print_path(const til_setting_desc_t *desc, FILE *out);
int til_setting_spec_check(const til_setting_spec_t *spec, const char *value);
int til_settings_label_setting(const til_settings_t *settings, const til_setting_t *setting, char **res_label);
int til_settings_print_path(const til_settings_t *settings, FILE *out);

#ifndef TIL_SETTINGS_STR
#define _TIL_SETTINGS_STR(s)	#s
#define TIL_SETTINGS_STR(s)	_TIL_SETTINGS_STR(s)
#endif

#endif
