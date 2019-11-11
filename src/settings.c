#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "util.h"

#ifdef __WIN32__
char * strndup(const char *s, size_t n)
{
	size_t	len;
	char	*buf;

	len = MIN(strlen(s), n);
	buf = calloc(len + 1, sizeof(char));
	if (!buf)
		return NULL;

	memcpy(buf, s, len);

	return buf;
}
#endif

/* Split form of key=value[,key=value...] settings string */
typedef struct settings_t {
	unsigned	num;
	const char	**keys;
	const char	**values;
} settings_t;

typedef enum settings_fsm_state_t {
	SETTINGS_FSM_STATE_KEY,
	SETTINGS_FSM_STATE_EQUAL,
	SETTINGS_FSM_STATE_VALUE,
	SETTINGS_FSM_STATE_COMMA,
} settings_fsm_state_t;


static int add_value(settings_t *settings, const char *key, const char *value)
{
	assert(settings);

	settings->num++;
	/* TODO errors */
	settings->keys = realloc(settings->keys, settings->num * sizeof(const char **));
	settings->values = realloc(settings->values, settings->num * sizeof(const char **));
	settings->keys[settings->num - 1] = key;
	settings->values[settings->num - 1] = value;

	return 0;
}


/* split settings_string into a data structure */
settings_t * settings_new(const char *settings_string)
{
	settings_fsm_state_t	state = SETTINGS_FSM_STATE_KEY;
	const char		*p, *token;
	settings_t		*settings;

	settings = calloc(1, sizeof(settings_t));
	if (!settings)
		return NULL;

	if (!settings_string)
		return settings;

	/* TODO: unescaping? */
	for (token = p = settings_string; ;p++) {

		switch (state) {
		case SETTINGS_FSM_STATE_COMMA:
			token = p;
			state = SETTINGS_FSM_STATE_KEY;
			break;

		case SETTINGS_FSM_STATE_KEY:
			if (*p == '=' || *p == ',' || *p == '\0') {
				add_value(settings, strndup(token, p - token), NULL);

				if (*p == '=')
					state = SETTINGS_FSM_STATE_EQUAL;
				else if (*p == ',')
					state = SETTINGS_FSM_STATE_COMMA;
			}
			break;

		case SETTINGS_FSM_STATE_EQUAL:
			token = p;
			state = SETTINGS_FSM_STATE_VALUE;
			break;

		case SETTINGS_FSM_STATE_VALUE:
			if (*p == ',' || *p == '\0') {
				settings->values[settings->num - 1] = strndup(token, p - token);
				state = SETTINGS_FSM_STATE_COMMA;
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
void settings_free(settings_t *settings)
{
	unsigned	i;

	assert(settings);

	for (i = 0; i < settings->num; i++) {
		free((void *)settings->keys[i]);
		free((void *)settings->values[i]);
	}

	free((void *)settings->keys);
	free((void *)settings->values);
	free(settings);
}


/* find key= in settings, return dup of value side or NULL if missing */
const char * settings_get_value(const settings_t *settings, const char *key)
{
	int	i;

	assert(settings);
	assert(key);

	for (i = 0; i < settings->num; i++) {
		if (!strcmp(key, settings->keys[i]))
			return settings->values[i];
	}

	return NULL;
}


/* return positional key from settings */
const char * settings_get_key(const settings_t *settings, unsigned pos)
{
	assert(settings);

	if (pos < settings->num)
		return settings->keys[pos];

	return NULL;
}


/* add key=value to the settings,
 * or just key if value is NULL.
 */
/* returns < 0 on error */
int settings_add_value(settings_t *settings, const char *key, const char *value)
{
	assert(settings);
	assert(key);

	return add_value(settings, strdup(key), value ? strdup(value) : NULL);
}


/* apply the supplied setting description generators to the supplied settings */
/* returns 0 when input settings are complete */
/* returns 1 when input settings are incomplete, storing the next setting's description needed in *next_setting */
/* returns -errno on error */
int settings_apply_desc_generators(const settings_t *settings, const setting_desc_generator_t generators[], unsigned n_generators, void *setup_context, setting_desc_t **next_setting)
{
	unsigned	i;
	setting_desc_t	*next;

	assert(settings);
	assert(generators);
	assert(n_generators > 0);
	assert(next_setting);

	for (i = 0; i < n_generators; i++) {
		const setting_desc_generator_t	*g = &generators[i];
		const char			*value;

		value = settings_get_value(settings, g->key);
		if (value) {
			if (g->value_ptr)
				*g->value_ptr = value;

			continue;
		}

		next = g->func(setup_context);
		if (!next)
			return -ENOMEM;

		*next_setting = next;

		return 1;
	}

	return 0;
}


/* convenience helper for creating a new setting description */
/* copies of everything supplied are made in newly allocated memory, stored @ res_desc */
/* returns < 0 on error */
int setting_desc_clone(const setting_desc_t *desc, setting_desc_t **res_desc)
{
	setting_desc_t	*d;

	assert(desc);
	assert(desc->name);
	assert(desc->preferred);	/* XXX: require a preferred default? */
	assert(!desc->annotations || desc->values);

	d = calloc(1, sizeof(setting_desc_t));
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

	/* TODO: handle allocation errors above... */
	*res_desc = d;

	return 0;
}


void setting_desc_free(setting_desc_t *desc)
{
	free((void *)desc->name);
	free((void *)desc->key);
	free((void *)desc->regex);
	free((void *)desc->preferred);

	if (desc->values) {
		unsigned	i;

		for (i = 0; desc->values[i]; i++) {
			free((void *)desc->values[i]);

			if (desc->annotations)
				free((void *)desc->annotations[i]);
		}

		free((void *)desc->values);
		free((void *)desc->annotations);
	}

	free(desc);
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


char * settings_as_arg(const settings_t *settings)
{
	char	*buf = NULL;
	size_t	off, size;

	/* intentionally avoided open_memstream for portability reasons */
	for (;;) {
		unsigned	i;

		for (i = off = 0; i < settings->num; i++) {
			if (i > 0)
				off += snpf(buf, size, off, ",");

			off += snpf(buf, size, off, "%s", settings->keys[i]);

			if (settings->values[i])
				off += snpf(buf, size, off, "=%s", settings->values[i]);
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
