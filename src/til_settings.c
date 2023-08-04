#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "til_settings.h"
#include "til_str.h"
#include "til_util.h"

#ifdef __WIN32__
char * strndup(const char *s, size_t n)
{
	size_t	len;
	char	*buf;

	for (len = 0; len < n; len++) {
		if (!s[len])
			break;
	}

	buf = calloc(len + 1, sizeof(char));
	if (!buf)
		return NULL;

	memcpy(buf, s, len);

	return buf;
}
#endif

/* Split form of key=value[,key=value...] settings string */
typedef struct til_settings_t {
	const til_settings_t	*parent;
	const char		*prefix;
	const char		*label;
	unsigned		num;
	til_setting_t		**entries;
} til_settings_t;

typedef enum til_settings_fsm_state_t {
	TIL_SETTINGS_FSM_STATE_KEY,
	TIL_SETTINGS_FSM_STATE_KEY_ESCAPED,
	TIL_SETTINGS_FSM_STATE_EQUAL,
	TIL_SETTINGS_FSM_STATE_VALUE,
	TIL_SETTINGS_FSM_STATE_VALUE_ESCAPED,
	TIL_SETTINGS_FSM_STATE_COMMA,
} til_settings_fsm_state_t;


static til_setting_t * add_setting(til_settings_t *settings, const char *key, const char *value, int nocheck)
{
	til_setting_t	**new_entries;
	til_setting_t	*s;

	assert(settings);

	s = calloc(1, sizeof(til_setting_t));
	if (!s)
		return NULL;

	s->parent = settings;
	s->key = key;
	s->value = value;
	s->nocheck = nocheck;

	new_entries = realloc(settings->entries, (settings->num + 1) * sizeof(til_setting_t *));
	if (!new_entries) {
		free(s);
		return NULL;
	}

	settings->entries = new_entries;
	settings->entries[settings->num] = s;
	settings->num++;

	return s;
}


/* split settings_string into a data structure */
til_settings_t * til_settings_new(const char *prefix, const til_settings_t *parent, const char *label, const char *settings_string)
{
	til_settings_fsm_state_t	state = TIL_SETTINGS_FSM_STATE_COMMA;
	const char			*p;
	til_settings_t			*settings;
	til_str_t			*value_str;

	assert(label);

	settings = calloc(1, sizeof(til_settings_t));
	if (!settings)
		goto _err;

	if (prefix) {
		settings->prefix = strdup(prefix);
		if (!settings->prefix)
			goto _err;
	}

	settings->parent = parent;
	settings->label = strdup(label);
	if (!settings->label)
		goto _err;

	if (!settings_string)
		return settings;

	for (p = settings_string;;p++) {

		switch (state) {
		case TIL_SETTINGS_FSM_STATE_COMMA:
			value_str = til_str_new("");
			if (!value_str)
				goto _err;

			state = TIL_SETTINGS_FSM_STATE_KEY;
			/* fallthrough */

		case TIL_SETTINGS_FSM_STATE_KEY:
			if (*p == '\\')
				state = TIL_SETTINGS_FSM_STATE_KEY_ESCAPED;
			else if (*p == '=' || *p == ',' || *p == '\0') {

				if (*p == '=') { /* key= */
					(void) add_setting(settings, til_str_to_buf(value_str, NULL), NULL, 0);
					state = TIL_SETTINGS_FSM_STATE_EQUAL;
				} else { /* bare value */
					char	*v = til_str_to_buf(value_str, NULL);
					int	nocheck = v[0] == ':' ? 1 : 0;

					if (nocheck)
						v++;

					(void) add_setting(settings, NULL, v, nocheck);
					state = TIL_SETTINGS_FSM_STATE_COMMA;
				}
			} else
				til_str_appendf(value_str, "%c", *p); /* FIXME: errors */
			break;

		case TIL_SETTINGS_FSM_STATE_KEY_ESCAPED:
			til_str_appendf(value_str, "%c", *p); /* FIXME: errors */
			state = TIL_SETTINGS_FSM_STATE_KEY;
			break;

		case TIL_SETTINGS_FSM_STATE_EQUAL:
			value_str = til_str_new("");
			if (!value_str)
				goto _err;

			state = TIL_SETTINGS_FSM_STATE_VALUE;
			/* fallthrough, necessary to not leave NULL values for empty "key=\0" settings */

		case TIL_SETTINGS_FSM_STATE_VALUE:
			if (*p == '\\')
				state = TIL_SETTINGS_FSM_STATE_VALUE_ESCAPED;
			else if (*p == ',' || *p == '\0') {
				char	*v = til_str_to_buf(value_str, NULL);
				int	r;

				r = til_setting_set_raw_value(settings->entries[settings->num - 1], v);
				free(v);
				if (r < 0)
					goto _err;

				state = TIL_SETTINGS_FSM_STATE_COMMA;
			} else
				til_str_appendf(value_str, "%c", *p); /* FIXME: errors */

			break;

		case TIL_SETTINGS_FSM_STATE_VALUE_ESCAPED:
			/* TODO: note currently we just pass-through literally whatever char was escaped,
			 * but in cases like \n it should really be turned into 0xa so we can actually have
			 * a setting contain a raw newline post-unescaping (imagine a marquee module supporting
			 * arbitray text to be drawn, newlines would be ok yeah? should it be responsible for
			 * unescaping "\n" itself or just look for '\n' (0xa) to insert linefeeds?  I think
			 * the latter...)  But until there's a real need for that, this can just stay as-is.
			 */
			til_str_appendf(value_str, "%c", *p); /* FIXME: errors */
			state = TIL_SETTINGS_FSM_STATE_VALUE;
			break;

		default:
			assert(0);
		}

		if (*p == '\0')
			break;
	}

	/* FIXME: this should probably never leave a value or key entry NULL */

	return settings;

_err:
	return til_settings_free(settings);
}


/* free structure attained via settings_new() */
til_settings_t * til_settings_free(til_settings_t *settings)
{

	if (settings) {
		for (unsigned i = 0; i < settings->num; i++) {
			if (settings->entries[i]->value_as_nested_settings)
				til_settings_free(settings->entries[i]->value_as_nested_settings);

			free((void *)settings->entries[i]->key);
			if (settings->entries[i]->value) {
				if (settings->entries[i]->nocheck)
					settings->entries[i]->value--;
				free((void *)settings->entries[i]->value);
			}
			til_setting_desc_free((void *)settings->entries[i]->desc);
			free((void *)settings->entries[i]);
		}

		free((void *)settings->entries);
		free((void *)settings->label);
		free((void *)settings->prefix);
		free(settings);
	}

	return NULL;
}


unsigned til_settings_get_count(const til_settings_t *settings)
{
	assert(settings);

	return settings->num;
}


const til_settings_t * til_settings_get_parent(const til_settings_t *settings)
{
	assert(settings);

	return settings->parent;
}


int til_settings_set_label(til_settings_t *settings, const char *label)
{
	char	*t;

	assert(settings);
	assert(label);

	t = strdup(label);
	if (!t)
		return -ENOMEM;

	free((void *)settings->label);
	settings->label = t;

	return 0;
}


const char * til_settings_get_label(const til_settings_t *settings)
{
	assert(settings);

	return settings->label;
}


/* find key= in settings, return value NULL if missing, optionally store setting @res_setting if found */
const char * til_settings_get_value_by_key(const til_settings_t *settings, const char *key, til_setting_t **res_setting)
{
	assert(settings);
	assert(key);

	for (int i = 0; i < settings->num; i++) {
		if (!settings->entries[i]->key)
			continue;

		if (!strcasecmp(key, settings->entries[i]->key)) {
			if (res_setting)
				*res_setting = settings->entries[i];

			return settings->entries[i]->value;
		}
	}

	return NULL;
}


/* return positional value from settings, NULL if missing, optionally store setting @res_setting if found */
const char * til_settings_get_value_by_idx(const til_settings_t *settings, unsigned idx, til_setting_t **res_setting)
{
	assert(settings);

	if (idx < settings->num) {
		if (res_setting)
			*res_setting = settings->entries[idx];

		return settings->entries[idx]->value;
	}

	return NULL;
}


/* helper for the common setup case of describing a setting when absent or not yet described.
 * returns:
 * -1 on error, res_* will be untouched in this case.
 * 0 when setting is present and described, res_value and res_setting will be populated w/non-NULL, and res_desc NULL in this case.
 * 1 when setting is either present but undescribed, or absent (and undescribed), res_* will be populated but res_{value,setting} may be NULL if absent and simply described.
 */
int til_settings_get_and_describe_value(const til_settings_t *settings, const til_setting_spec_t *spec, const char **res_value, til_setting_t **res_setting, const til_setting_desc_t **res_desc)
{
	til_setting_t	*setting;
	const char	*value;

	assert(settings);
	assert(spec);
	assert(res_value);

	value = til_settings_get_value_by_key(settings, spec->key, &setting);
	if (!value || !setting->desc) {
		int	r;

		assert(res_setting);
		assert(res_desc);

		r = til_setting_desc_new(settings, spec, res_desc);
		if (r < 0)
			return r;

		*res_value = value;
		*res_setting = value ? setting : NULL;

		return 1;
	}

	*res_value = value;
	if (res_setting)
		*res_setting = setting;
	if (res_desc)
		*res_desc = NULL;

	return 0;
}


/* add key,value as a new setting to settings,
 * NULL keys are passed through as-is
 * values must not be NULL
 */
/* returns the added setting, or NULL on error (ENOMEM) */
til_setting_t * til_settings_add_value(til_settings_t *settings, const char *key, const char *value)
{
	int	nocheck = 0;
	char	*v;

	assert(settings);
	assert(value);
	/* XXX: ^^ non-NULL values makes til_settings_get_value_by_idx() NULL-return-for-end-of-settings OK */

	v = strdup(value);
	if (!v)
		return NULL;

	if (v[0] == ':') {
		nocheck = 1;
		v++;
	}

	return add_setting(settings, key ? strdup(key) : NULL, v, nocheck);
}


void til_settings_reset_descs(til_settings_t *settings)
{
	assert(settings);

	for (unsigned i = 0; i < settings->num; i++)
		settings->entries[i]->desc = til_setting_desc_free(settings->entries[i]->desc);
}


/* apply the supplied setting description generators to the supplied settings */
/* returns 0 when input settings are complete */
/* returns 1 when input settings are incomplete, storing the next setting's description needed in *next_setting */
/* returns -errno on error */
int til_settings_apply_desc_generators(const til_settings_t *settings, const til_setting_desc_generator_t generators[], unsigned n_generators, til_setup_t *setup, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	assert(settings);
	assert(generators);
	assert(n_generators > 0);
	assert(res_setting);
	assert(res_desc);

	for (unsigned i = 0; i < n_generators; i++) {
		const til_setting_desc_generator_t	*g = &generators[i];
		const til_setting_desc_t		*desc;
		int					r;

		r = g->func(settings, setup, &desc);
		if (r < 0)
			return r;

		r = til_settings_get_and_describe_value(settings, &desc->spec, g->value_ptr, res_setting, res_desc);
		til_setting_desc_free(desc); /* always need to cleanup the desc from g->func(), res_desc gets its own copy */
		if (r)
			return r;
	}

	if (res_setup)
		*res_setup = setup;

	return 0;
}


/* convenience helper for creating a new setting description */
/* copies of everything supplied are made in newly allocated memory, stored @ res_desc */
/* returns < 0 on error */
int til_setting_desc_new(const til_settings_t *settings, const til_setting_spec_t *spec, const til_setting_desc_t **res_desc)
{
	til_setting_desc_t	*d;

	assert(settings);
	assert(spec);
	if (!spec->as_nested_settings) { /* this feels dirty, but sometimes you just need a bare nested settings created */
		assert(spec->name);
		assert(spec->preferred);	/* XXX: require a preferred default? */
	}
	assert((!spec->annotations || spec->values) || spec->as_nested_settings);
	assert(res_desc);

	d = calloc(1, sizeof(til_setting_desc_t));
	if (!d)
		return -ENOMEM;

	/* XXX: intentionally casting away the const here, since the purpose of desc->container is to point where to actually put the setting for the front-end setup code */
	d->container = (til_settings_t *)settings;

	if (spec->name)
		d->spec.name = strdup(spec->name);
	if (spec->key)	/* This is inappropriately subtle, but when key is NULL, the value will be the key, and there will be no value side at all. */
		d->spec.key = strdup(spec->key);
	if (spec->regex)
		d->spec.regex = strdup(spec->regex);

	if (spec->preferred)
		d->spec.preferred = strdup(spec->preferred);

	if (spec->values) {
		unsigned	i;

		for (i = 0; spec->values[i]; i++);

		d->spec.values = calloc(i + 1, sizeof(*spec->values));

		if (spec->annotations)
			d->spec.annotations = calloc(i + 1, sizeof(*spec->annotations));

		for (i = 0; spec->values[i]; i++) {
			d->spec.values[i] = strdup(spec->values[i]);

			if (spec->annotations) {
				assert(spec->annotations[i]);
				d->spec.annotations[i] = strdup(spec->annotations[i]);
			}
		}
	}

	d->spec.random = spec->random;
	d->spec.override = spec->override;
	d->spec.as_nested_settings = spec->as_nested_settings;
	d->spec.as_label = spec->as_label;

	/* TODO: handle allocation errors above... */
	*res_desc = d;

	return 0;
}


til_setting_desc_t * til_setting_desc_free(const til_setting_desc_t *desc)
{
	if (desc) {
		free((void *)desc->spec.name);
		free((void *)desc->spec.key);
		free((void *)desc->spec.regex);
		free((void *)desc->spec.preferred);

		if (desc->spec.values) {
			for (unsigned i = 0; desc->spec.values[i]; i++) {
				free((void *)desc->spec.values[i]);

				if (desc->spec.annotations)
					free((void *)desc->spec.annotations[i]);
			}

			free((void *)desc->spec.values);
			free((void *)desc->spec.annotations);
		}

		free((void *)desc);
	}

	return NULL;
}



int til_setting_desc_strprint_path(const til_setting_desc_t *desc, til_str_t *str)
{
	int	r;

	assert(desc);
	assert(str);

	r = til_settings_strprint_path(desc->container, str);
	if (r < 0)
		return r;

	/* XXX: spec.as_label handling is done in til_settings_print_path() since it
	 * must apply anywhere within a path, potentially in a recurring fashion.
	 * So all we're dealing with here is tacking a potentially named desc onto the end,
	 * treating named descs as a sort of leaf of the path.  Though the desc may actually
	 * describe a setting w/nested settings, i.e. it doesn't actually have to be a leaf
	 * for this to be correct.  When its a parent of nested settings scenario, its key
	 * would have been used to label the nested settings, but this print won't traverse
	 * down from desc->container, only up the parents.
	 */
	if (desc->spec.key) {
		r = til_str_appendf(str, "/%s", desc->spec.key);
		if (r < 0)
			return r;
	}

	return 0;
}


int til_setting_desc_fprint_path(const til_setting_desc_t *desc, FILE *out)
{
	til_str_t	*str;
	int		r;

	assert(desc);
	assert(out);

	str = til_str_new("");
	if (!str)
		return -ENOMEM;

	r = til_setting_desc_strprint_path(desc, str);
	if (r < 0) {
		til_str_free(str);
		return r;
	}

	if (fputs(til_str_buf(str, NULL), out) == EOF)
		r = -EPIPE;

	til_str_free(str);

	return r;
}


/* TODO: spec checking in general needs refinement and to be less intolerant of
 * creative experimentation.
 */
/* Check's setting's value against the provided spec.
 * If setting->nocheck is set the check is skipped.
 * If spec->as_nested_settings is set, no check is performed, as it's not really applicable until leaf settings
 */
int til_setting_check_spec(const til_setting_t *setting, const til_setting_spec_t *spec)
{
	assert(spec);
	assert(setting);
	assert(setting->value);

	if (setting->nocheck)
		return 0;

	/* XXX: this check can't really be performed on anything but "leaf" settings. */
	if (spec->values && !spec->as_nested_settings) {

		for (int i = 0; spec->values[i]; i++) {
			if (!strcasecmp(spec->values[i], setting->value))
				return 0;
		}

		/* TODO: there probably needs to be a way to make this less fatal
		 * in the spec and/or at runtime via a flag.  The values[] are more like presets,
		 * and especially for numeric settings we should be able to explicitly specify a
		 * perfectly usable number that isn't within the presets, if the module can live
		 * with it (think arbitrary floats)...
		 */
		return -EINVAL;
	}

	/* TODO: apply regex check */

	return 0;
}


/* helper for changing the "raw" value of a setting, maintains til_setting_t.nocheck */
int til_setting_set_raw_value(til_setting_t *setting, const char *value)
{
	int		nocheck = 0;
	const char	*v;

	assert(setting);
	assert(value);

	v = strdup(value);
	if (!v)
		return -ENOMEM;

	if (v[0] == ':') {
		nocheck = 1;
		v++;
	}

	if (setting->value) {
		if (setting->nocheck)
			setting->value--;
		free((void *)setting->value);
	}

	setting->value = v;
	setting->nocheck = nocheck;

	return 0;
}


/* helper for accessing the "raw" value for a setting, which presently just means
 * if a value was added as a "nocheck" value with a ':' prefix, this will return the
 * prefixed form.  Otherwise you just get the same thing as setting->value.
 */
const char * til_setting_get_raw_value(til_setting_t *setting)
{
	assert(setting);

	if (setting->nocheck)
		return setting->value - 1;

	return setting->value;
}



static inline void fputc_escaped(til_str_t *out, int c, unsigned depth)
{
	unsigned	escapes = 0;

	for (unsigned i = 0; i < depth; i++) {
		escapes <<= 1;
		escapes += 1;
	}

	for (unsigned i = 0; i < escapes; i++)
		til_str_appendf(out, "\\");

	til_str_appendf(out, "%c", c);
}


static inline void fputs_escaped(til_str_t *out, const char *value, unsigned depth)
{
	char	c;

	while ((c = *value++)) {
		switch (c) {
		case '\'': /* this isn't strictly necessary, but let's just make settings-as-arg easily quotable for shell purposes, excessive escaping is otherwise benign */
		case '=':
		case ',':
		case '\\':
			fputc_escaped(out, c, depth);
			break;
		default:
			til_str_appendf(out, "%c", c);
			break;
		}
	}
}


static int settings_as_arg(const til_settings_t *settings, int unfiltered, unsigned depth, til_str_t *out)
{
	for (size_t i = 0, j = 0; i < settings->num; i++) {
		if (!unfiltered && !settings->entries[i]->desc)
			continue;

		/* FIXME TODO: detect errors */
		if (j > 0)
			fputc_escaped(out, ',', depth);

		if (settings->entries[i]->key) {
			fputs_escaped(out, settings->entries[i]->key, depth);
			if (settings->entries[i]->value)
				fputc_escaped(out, '=', depth);
		}

		if (settings->entries[i]->value_as_nested_settings) {
			settings_as_arg(settings->entries[i]->value_as_nested_settings, unfiltered, depth + 1, out);
		} else if (settings->entries[i]->value) {
			const char	*v = til_setting_get_raw_value(settings->entries[i]);

			fputs_escaped(out, v, depth);
		}
		j++;
	}

	return 0;
}


static char * _settings_as_arg(const til_settings_t *settings, int unfiltered)
{
	til_str_t	*str;

	str = til_str_new("");
	if (!str)
		return NULL;

	if (settings_as_arg(settings, unfiltered, 0, str) < 0)
		return til_str_free(str);

	return til_str_to_buf(str, NULL);
}


/* returns the serialized form of settings usable as a cli argument, omitting any undescribed settings */
char * til_settings_as_arg(const til_settings_t *settings)
{
	return _settings_as_arg(settings, 0);
}


/* same as til_settings_as_arg() but including undescribed settings */
char * til_settings_as_arg_unfiltered(const til_settings_t *settings)
{
	return _settings_as_arg(settings, 1);
}


/* generate a positional label for a given setting, stored @ res_label.
 * this is added specifically for labeling bare-value settings in an array subscript fashion...
 */
int til_settings_label_setting(const til_settings_t *settings, const til_setting_t *setting, char **res_label)
{
	char	*label;

	assert(settings && settings->label);
	assert(setting);
	assert(res_label);

	/* Have to search for the setting, but shouldn't be perf-sensitive
	 * since we don't do stuff like this every frame or anything.
	 * I suppose til_setting_t could cache its position when added... TODO
	 */
	for (unsigned i = 0; i < settings->num; i++) {
		if (settings->entries[i] == setting) {
			size_t	len = snprintf(NULL, 0, "[%u]", i) + 1;

			label = calloc(1, len);
			if (!label)
				return -ENOMEM;

			snprintf(label, len, "[%u]", i);
			*res_label = label;

			return 0;
		}
	}

	return -ENOENT;
}


int til_settings_strprint_path(const til_settings_t *settings, til_str_t *str)
{
	const til_settings_t	*p, *parents[64];
	size_t			i, n_parents;

	assert(settings);
	assert(str);

	for (p = settings, n_parents = 0; p != NULL; p = p->parent)
		n_parents++;

	/* XXX: if this limitation becomes a problem we can always malloc()
	 * space, but I can't imagine such deep settings heirarchies being real.
	 */
	assert(n_parents <= nelems(parents));

	for (i = 0, p = settings; i < n_parents; p = p->parent, i++)
		parents[n_parents - i - 1] = p;

	for (i = 0; i < n_parents; i++) {
		int	r;

		if (parents[i]->prefix) {
			r = til_str_appendf(str, "%s", parents[i]->prefix);
			if (r < 0)
				return r;
		}

		r = til_str_appendf(str, "/%s", parents[i]->label);
		if (r < 0)
			return r;

		if (parents[i]->num > 0 &&
		    parents[i]->entries[0]->desc &&
		    parents[i]->entries[0]->desc->spec.as_label) {

			r = til_str_appendf(str, "/%s", parents[i]->entries[0]->value);
			if (r < 0)
				return r;
		}
	}

	return 0;
}


/*
 * returns a raw path to settings in *res_buf
 * if res_len is provided, the returned string length excluding nul is stored there (til_str_to_buf())
 */
static int til_settings_path_as_buf(const til_settings_t *settings, char **res_buf, size_t *res_len)
{
	til_str_t	*str;
	int		r;

	assert(settings);
	assert(res_buf);

	str = til_str_new("");
	if (!str)
		return -ENOMEM;

	r = til_settings_strprint_path(settings, str);
	if (r < 0)
		return r;

	*res_buf = til_str_to_buf(str, res_len);

	return 0;
}



int til_settings_fprint_path(const til_settings_t *settings, FILE *out)
{
	char	*buf;
	int	r = 0;

	assert(settings);
	assert(out);

	r = til_settings_path_as_buf(settings, &buf, NULL);
	if (r < 0)
		return r;

	if (fputs(buf, out) == EOF)
		r = -EPIPE;

	free(buf);

	return r;
}
