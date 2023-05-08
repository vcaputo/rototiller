#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "til_settings.h"
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
	const char	*label;
	unsigned	num;
	til_setting_t	**settings;
} til_settings_t;

typedef enum til_settings_fsm_state_t {
	TIL_SETTINGS_FSM_STATE_KEY,
	TIL_SETTINGS_FSM_STATE_KEY_ESCAPED,
	TIL_SETTINGS_FSM_STATE_EQUAL,
	TIL_SETTINGS_FSM_STATE_VALUE,
	TIL_SETTINGS_FSM_STATE_VALUE_ESCAPED,
	TIL_SETTINGS_FSM_STATE_COMMA,
} til_settings_fsm_state_t;


static til_setting_t * add_setting(til_settings_t *settings, const char *key, const char *value, const til_setting_desc_t *desc)
{
	til_setting_t	**new_settings;
	til_setting_t	*s;

	assert(settings);

	s = calloc(1, sizeof(til_setting_t));
	if (!s)
		return NULL;

	new_settings = realloc(settings->settings, (settings->num + 1) * sizeof(til_setting_t *));
	if (!new_settings) {
		free(s);
		return NULL;
	}

	settings->settings = new_settings;
	settings->settings[settings->num] = s;
	settings->settings[settings->num]->key = key;
	settings->settings[settings->num]->value = value;
	settings->settings[settings->num]->desc = desc;
	settings->num++;

	return s;
}


/* split settings_string into a data structure */
til_settings_t * til_settings_new(const char *label, const char *settings_string)
{
	til_settings_fsm_state_t	state = TIL_SETTINGS_FSM_STATE_COMMA;
	const char			*p;
	til_settings_t			*settings;
	FILE				*value_fp;
	char				*value_buf;
	size_t				value_sz;

	assert(label);

	settings = calloc(1, sizeof(til_settings_t));
	if (!settings)
		goto _err;

	settings->label = strdup(label);
	if (!settings->label)
		goto _err;

	if (!settings_string)
		return settings;

	for (p = settings_string;;p++) {

		switch (state) {
		case TIL_SETTINGS_FSM_STATE_COMMA:
			value_fp = open_memstream(&value_buf, &value_sz); /* TODO FIXME: open_memstream() isn't portable */
			if (!value_fp)
				goto _err;

			state = TIL_SETTINGS_FSM_STATE_KEY;
			/* fallthrough */

		case TIL_SETTINGS_FSM_STATE_KEY:
			if (*p == '\\')
				state = TIL_SETTINGS_FSM_STATE_KEY_ESCAPED;
			else if (*p == '=' || *p == ',' || *p == '\0') {
				fclose(value_fp);

				if (*p == '=') { /* key= */
					(void) add_setting(settings, value_buf, NULL, NULL);
					state = TIL_SETTINGS_FSM_STATE_EQUAL;
				} else { /* bare value */
					(void) add_setting(settings, NULL, value_buf, NULL);
					state = TIL_SETTINGS_FSM_STATE_COMMA;
				}
			} else
				fputc(*p, value_fp);
			break;

		case TIL_SETTINGS_FSM_STATE_KEY_ESCAPED:
			fputc(*p, value_fp);
			state = TIL_SETTINGS_FSM_STATE_KEY;
			break;

		case TIL_SETTINGS_FSM_STATE_EQUAL:
			value_fp = open_memstream(&value_buf, &value_sz); /* TODO FIXME: open_memstream() isn't portable */
			if (!value_fp)
				goto _err;

			state = TIL_SETTINGS_FSM_STATE_VALUE;
			/* fallthrough, necessary to not leave NULL values for empty "key=\0" settings */

		case TIL_SETTINGS_FSM_STATE_VALUE:
			if (*p == '\\')
				state = TIL_SETTINGS_FSM_STATE_VALUE_ESCAPED;
			else if (*p == ',' || *p == '\0') {
				fclose(value_fp);
				settings->settings[settings->num - 1]->value = value_buf;
				state = TIL_SETTINGS_FSM_STATE_COMMA;
			} else
				fputc(*p, value_fp);

			break;

		case TIL_SETTINGS_FSM_STATE_VALUE_ESCAPED:
			/* TODO: note currently we just pass-through literally whatever char was escaped,
			 * but in cases like \n it should really be turned into 0xa so we can actually have
			 * a setting contain a raw newline post-unescaping (imagine a marquee module supporting
			 * arbitray text to be drawn, newlines would be ok yeah? should it be responsible for
			 * unescaping "\n" itself or just look for '\n' (0xa) to insert linefeeds?  I think
			 * the latter...)  But until there's a real need for that, this can just stay as-is.
			 */
			fputc(*p, value_fp);
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
			free((void *)settings->settings[i]->key);
			free((void *)settings->settings[i]->value);
			til_setting_desc_free((void *)settings->settings[i]->desc);
			free((void *)settings->settings[i]);
		}

		free((void *)settings->settings);
		free(settings);
	}

	return NULL;
}


/* find key= in settings, return value NULL if missing, optionally store setting @res_setting if found */
const char * til_settings_get_value_by_key(const til_settings_t *settings, const char *key, til_setting_t **res_setting)
{
	assert(settings);
	assert(key);

	for (int i = 0; i < settings->num; i++) {
		if (!settings->settings[i]->key)
			continue;

		if (!strcasecmp(key, settings->settings[i]->key)) {
			if (res_setting)
				*res_setting = settings->settings[i];

			return settings->settings[i]->value;
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
			*res_setting = settings->settings[idx];

		return settings->settings[idx]->value;
	}

	return NULL;
}


/* helper for the common setup case of describing a setting when absent or not yet described.
 * returns:
 * -1 on error, res_* will be untouched in this case.
 * 0 when setting is present and described, res_value and res_setting will be populated w/non-NULL, and res_desc NULL in this case.
 * 1 when setting is either present but undescribed, or absent (and undescribed), res_* will be populated but res_{value,setting} may be NULL if absent and simply described.
 */
int til_settings_get_and_describe_value(const til_settings_t *settings, const til_setting_desc_t *desc, const char **res_value, til_setting_t **res_setting, const til_setting_desc_t **res_desc)
{
	til_setting_t	*setting;
	const char	*value;

	assert(settings);
	assert(desc);
	assert(res_value);

	value = til_settings_get_value_by_key(settings, desc->key, &setting);
	if (!value || !setting->desc) {
		int	r;

		assert(res_setting);
		assert(res_desc);

		r = til_setting_desc_clone(desc, res_desc);
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
 * NULL keys and/or values are passed through as-is
 * desc may be NULL, it's simply passed along as a passenger.
 */
/* returns the added setting, or NULL on error (ENOMEM) */
til_setting_t * til_settings_add_value(til_settings_t *settings, const char *key, const char *value, const til_setting_desc_t *desc)
{
	assert(settings);

	return add_setting(settings, key ? strdup(key) : NULL, value ? strdup(value) : NULL, desc);
}


void til_settings_reset_descs(til_settings_t *settings)
{
	assert(settings);

	for (unsigned i = 0; i < settings->num; i++)
		settings->settings[i]->desc = NULL;
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

		r = g->func(setup, &desc);
		if (r < 0)
			return r;

		r = til_settings_get_and_describe_value(settings, desc, g->value_ptr, res_setting, res_desc);
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
int til_setting_desc_clone(const til_setting_desc_t *desc, const til_setting_desc_t **res_desc)
{
	til_setting_desc_t	*d;

	assert(desc);
	assert(desc->name);
	assert(desc->preferred);	/* XXX: require a preferred default? */
	assert(!desc->annotations || desc->values);
	assert(res_desc);

	d = calloc(1, sizeof(til_setting_desc_t));
	if (!d)
		return -ENOMEM;

	d->name = strdup(desc->name);
	if (desc->key)	/* This is inappropriately subtle, but when key is NULL, the value will be the key, and there will be no value side at all. */
		d->key = strdup(desc->key);
	if (desc->regex)
		d->regex = strdup(desc->regex);

	d->preferred = strdup(desc->preferred);

	if (desc->values) {
		unsigned	i;

		for (i = 0; desc->values[i]; i++);

		d->values = calloc(i + 1, sizeof(*desc->values));

		if (desc->annotations)
			d->annotations = calloc(i + 1, sizeof(*desc->annotations));

		for (i = 0; desc->values[i]; i++) {
			d->values[i] = strdup(desc->values[i]);

			if (desc->annotations) {
				assert(desc->annotations[i]);
				d->annotations[i] = strdup(desc->annotations[i]);
			}
		}
	}

	d->random = desc->random;

	/* TODO: handle allocation errors above... */
	*res_desc = d;

	return 0;
}


til_setting_desc_t * til_setting_desc_free(const til_setting_desc_t *desc)
{
	if (desc) {
		free((void *)desc->name);
		free((void *)desc->key);
		free((void *)desc->regex);
		free((void *)desc->preferred);

		if (desc->values) {
			for (unsigned i = 0; desc->values[i]; i++) {
				free((void *)desc->values[i]);

				if (desc->annotations)
					free((void *)desc->annotations[i]);
			}

			free((void *)desc->values);
			free((void *)desc->annotations);
		}

		free((void *)desc);
	}

	return NULL;
}


int til_setting_desc_check(const til_setting_desc_t *desc, const char *value)
{
	assert(desc);
	assert(value);

	if (desc->values) {

		for (int i = 0; desc->values[i]; i++) {
			if (!strcasecmp(desc->values[i], value))
				return 0;
		}

		return -EINVAL;
	}

	/* TODO: apply regex check */

	return 0;
}


/* wrapper around sprintf for convenient buffer size computation */
/* supply NULL buf when computing size, size and offset are ignored.
 * supply non-NULL for actual writing into buf of size bytes @ offset.
 * return value is number of bytes (potentially if !buf) written
 */
static int snpf(char *buf, size_t size, off_t offset, const char *format, ...)
{
	size_t	avail = 0;
	va_list	ap;
	int	r;

	if (buf) {
		assert(size > offset);

		avail = size - offset;
		buf += offset;
	}

	va_start(ap, format);
	r = vsnprintf(buf, avail, format, ap);
	va_end(ap);

	return r;
}


char * til_settings_as_arg(const til_settings_t *settings)
{
	char	*buf = NULL;
	size_t	off, size;

	/* intentionally avoided open_memstream for portability reasons */
	for (;;) {
		unsigned	i;

		for (i = off = 0; i < settings->num; i++) {
			if (i > 0)
				off += snpf(buf, size, off, ",");

			off += snpf(buf, size, off, "%s", settings->settings[i]->key);

			if (settings->settings[i]->value)
				off += snpf(buf, size, off, "=%s", settings->settings[i]->value);
		}

		if (!buf) {
			size = off + 1;
			buf = calloc(size, sizeof(char));
			if (!buf)
				return NULL;

			continue;
		}

		break;
	}

	return buf;
}
