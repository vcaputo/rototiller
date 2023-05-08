#include <assert.h>
#include <stdio.h>

#include "setup.h"
#include "til_settings.h"
#include "til_setup.h"
#include "til_util.h"

/* add key=value, but if key is NULL, use value as key. */
static int add_value(til_settings_t *settings, const char *key, const char *value)
{
	assert(settings);

	if (!key) {
		key = value;
		value = NULL;
	}

	assert(key);

	return til_settings_add_value(settings, key, value, NULL);
}


/* returns negative on error, otherwise number of additions made to settings */
int setup_interactively(til_settings_t *settings, int (*setup_func)(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup), int defaults, til_setup_t **res_setup, const til_setting_desc_t **res_failed_desc)
{
	unsigned			additions = 0;
	char				buf[256] = "\n";
	til_setting_t			*setting;
	const til_setting_desc_t	*desc;
	int				r;

	assert(settings);
	assert(setup_func);

	/* TODO: regex and error handling */

	while ((r = setup_func(settings, &setting, &desc, res_setup)) > 0) {
		additions++;

		/* if setup_func() has returned a description for an undescribed preexisting setting,
		 * validate its value against the description and assign the description if it passes.
		 */
		if (setting && !setting->desc) {
			 /* XXX FIXME: this key as value exception is janky, make a helper to access the value or stop doing that. */
			r = til_setting_desc_check(desc, setting->value ? : setting->key);
			if (r < 0) {
				*res_failed_desc = desc;

				return r;
			}

			if (desc->as_nested_settings && !setting->settings) {
				setting->settings = til_settings_new(setting->key, setting->value);
				if (!setting->settings) {
					*res_failed_desc = desc;

					/* FIXME: til_settings_new() seems like it should return an errno, since it can encounter parse errors too? */
					return -ENOMEM;
				};
			}

			setting->desc = desc;

			continue;
		}

		if (!defaults)
			puts("");

		if (desc->values) {
			unsigned	i, preferred = 0;
			int		width = 0;

			for (i = 0; desc->values[i]; i++) {
				int	len;

				len = strlen(desc->values[i]);
				if (len > width)
					width = len;
			}

			/* multiple choice */
			if (!defaults)
				printf("%s:\n", desc->name);

			for (i = 0; desc->values[i]; i++) {
				if (!defaults)
					printf("%2u: %*s%s%s\n", i, width, desc->values[i],
						desc->annotations ? ": " : "",
						desc->annotations ? desc->annotations[i] : "");

				if (!strcasecmp(desc->preferred, desc->values[i]))
					preferred = i;
			}

			if (!defaults)
				printf("Enter a value 0-%u [%u (%s)]: ",
					i - 1, preferred, desc->preferred);
		} else {
			/* arbitrarily typed input */
			if (!defaults)
				printf("%s [%s]: ", desc->name, desc->preferred);
		}

		if (!defaults) {
			fflush(stdout);

			if (feof(stdin))
				return -EIO;

			fgets(buf, sizeof(buf), stdin);
		}

		if (*buf == '\n') {
			/* accept preferred */
			add_value(settings, desc->key, desc->preferred);
		} else {
			buf[strlen(buf) - 1] = '\0';

			if (desc->values) {
				unsigned	i, j, found;

				/* multiple choice, map numeric input to values entry */
				if (sscanf(buf, "%u", &j) < 1) {
					printf("Invalid input: \"%s\"\n", buf);

					goto _next;
				}

				for (found = i = 0; desc->values[i]; i++) {
					if (i == j) {
						add_value(settings, desc->key, desc->values[i]);
						found = 1;
						break;
					}
				}

				if (!found) {
					printf("Invalid option: %u outside of range [0-%u]\n",
						j, i - 1);

					goto _next;
				}

			} else {
				/* use typed input as setting, TODO: apply regex */
				add_value(settings, desc->key, buf);
			}
		}
_next:
		til_setting_desc_free(desc);
	}

	return r < 0 ? r : additions;
}
