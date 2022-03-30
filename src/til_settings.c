#include <assert.h>
#include <errno.h>
#include <stdarg.h>
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
	unsigned	num;
	til_setting_t	**settings;
} til_settings_t;

typedef enum til_settings_fsm_state_t {
	TIL_SETTINGS_FSM_STATE_KEY,
	TIL_SETTINGS_FSM_STATE_EQUAL,
	TIL_SETTINGS_FSM_STATE_VALUE,
	TIL_SETTINGS_FSM_STATE_COMMA,
} til_settings_fsm_state_t;


static int add_value(til_settings_t *settings, const char *key, const char *value, const til_setting_desc_t *desc)
{
	assert(settings);

	settings->num++;
	/* TODO errors */
	settings->settings = realloc(settings->settings, settings->num * sizeof(til_setting_t *));
	settings->settings[settings->num - 1] = malloc(sizeof(til_setting_t));
	settings->settings[settings->num - 1]->key = key;
	settings->settings[settings->num - 1]->value = value;
	settings->settings[settings->num - 1]->desc = desc;
	settings->settings[settings->num - 1]->user_data = NULL;

	return 0;
}


/* split settings_string into a data structure */
til_settings_t * til_settings_new(const char *settings_string)
{
	til_settings_fsm_state_t	state = TIL_SETTINGS_FSM_STATE_KEY;
	const char			*p, *token;
	til_settings_t			*settings;

	settings = calloc(1, sizeof(til_settings_t));
	if (!settings)
		return NULL;

	if (!settings_string)
		return settings;

	/* TODO: unescaping? */
	for (token = p = settings_string; ;p++) {

		switch (state) {
		case TIL_SETTINGS_FSM_STATE_COMMA:
			token = p;
			state = TIL_SETTINGS_FSM_STATE_KEY;
			break;

		case TIL_SETTINGS_FSM_STATE_KEY:
			if (*p == '=' || *p == ',' || *p == '\0') {
				add_value(settings, strndup(token, p - token), NULL, NULL);

				if (*p == '=')
					state = TIL_SETTINGS_FSM_STATE_EQUAL;
				else if (*p == ',')
					state = TIL_SETTINGS_FSM_STATE_COMMA;
			}
			break;

		case TIL_SETTINGS_FSM_STATE_EQUAL:
			token = p;
			state = TIL_SETTINGS_FSM_STATE_VALUE;
			break;

		case TIL_SETTINGS_FSM_STATE_VALUE:
			if (*p == ',' || *p == '\0') {
				settings->settings[settings->num - 1]->value = strndup(token, p - token);
				state = TIL_SETTINGS_FSM_STATE_COMMA;
			}
			break;

		default:
			assert(0);
		}

		if (*p == '\0')
			break;
	}

	/* FIXME: this should probably never leave a value or key entry NULL */

	return settings;
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


/* find key= in settings, return dup of value side or NULL if missing */
const char * til_settings_get_value(const til_settings_t *settings, const char *key, til_setting_t **res_setting)
{
	assert(settings);
	assert(key);

	for (int i = 0; i < settings->num; i++) {
		if (!strcmp(key, settings->settings[i]->key)) {
			if (res_setting)
				*res_setting = settings->settings[i];

			return settings->settings[i]->value;
		}
	}

	return NULL;
}


/* return positional key from settings */
const char * til_settings_get_key(const til_settings_t *settings, unsigned pos, til_setting_t **res_setting)
{
	assert(settings);

	if (pos < settings->num) {
		if (res_setting)
			*res_setting = settings->settings[pos];

		return settings->settings[pos]->key;
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
	assert(res_setting);
	assert(res_desc);

	value = til_settings_get_value(settings, desc->key, &setting);
	if (!value || !setting->desc) {
		int	r;

		r = til_setting_desc_clone(desc, res_desc);
		if (r < 0)
			return r;

		*res_value = value;
		*res_setting = value ? setting : NULL;

		return 1;
	}

	*res_value = value;
	*res_setting = setting;
	*res_desc = NULL;

	return 0;
}


/* add key=value to the settings,
 * or just key if value is NULL.
 * desc may be NULL, it's simply passed along as a passenger.
 */
/* returns < 0 on error */
int til_settings_add_value(til_settings_t *settings, const char *key, const char *value, const til_setting_desc_t *desc)
{
	assert(settings);
	assert(key);

	return add_value(settings, strdup(key), value ? strdup(value) : NULL, desc);
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
int til_settings_apply_desc_generators(const til_settings_t *settings, const til_setting_desc_generator_t generators[], unsigned n_generators, void *setup_context, til_setting_t **res_setting, const til_setting_desc_t **res_desc)
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

		r = g->func(setup_context, &desc);
		if (r < 0)
			return r;

		r = til_settings_get_and_describe_value(settings, desc, g->value_ptr, res_setting, res_desc);
		til_setting_desc_free(desc); /* always need to cleanup the desc from g->func(), res_desc gets its own copy */
		if (r)
			return r;
	}

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
