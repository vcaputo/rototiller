#include <assert.h>
#include <stdio.h>

#include "setup.h"
#include "til_settings.h"
#include "til_setup.h"
#include "til_util.h"


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
		assert(desc);

		additions++;

		/* if setup_func() has returned a description for an undescribed preexisting setting,
		 * validate its value against the description and assign the description if it passes.
		 */
		if (setting && !setting->desc) {
			r = til_setting_spec_check(&desc->spec, setting->value);
			if (r < 0) {
				*res_failed_desc = desc;

				return r;
			}

			if (desc->spec.as_nested_settings && !setting->value_as_nested_settings) {
				char	*label = NULL;

				if (!desc->spec.key) {
					/* generate a positional label for bare-value specs */
					r = til_settings_label_setting(desc->container, setting, &label);
					if (r < 0)
						return r;
				}

				setting->value_as_nested_settings = til_settings_new(desc->container, desc->spec.key ? : label, setting->value);
				free(label);

				if (!setting->value_as_nested_settings) {
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

		if (desc->spec.values) {
			unsigned	i, preferred = 0;
			int		width = 0;

			for (i = 0; desc->spec.values[i]; i++) {
				int	len;

				len = strlen(desc->spec.values[i]);
				if (len > width)
					width = len;
			}

			/* multiple choice */
			if (!defaults)
				printf("%s:\n", desc->spec.name);

			for (i = 0; desc->spec.values[i]; i++) {
				if (!defaults)
					printf("%2u: %*s%s%s\n", i, width, desc->spec.values[i],
						desc->spec.annotations ? ": " : "",
						desc->spec.annotations ? desc->spec.annotations[i] : "");

				if (!strcasecmp(desc->spec.preferred, desc->spec.values[i]))
					preferred = i;
			}

			if (!defaults)
				printf("Enter a value 0-%u [%u (%s)]: ",
					i - 1, preferred, desc->spec.preferred);
		} else {
			/* arbitrarily typed input */
			if (!defaults)
				printf("%s [%s]: ", desc->spec.name, desc->spec.preferred);
		}

		if (!defaults) {
			fflush(stdout);

			if (feof(stdin))
				return -EIO;

			fgets(buf, sizeof(buf), stdin);
		}

		if (*buf == '\n') {
			/* accept preferred */
			til_settings_add_value(desc->container, desc->spec.key, desc->spec.preferred, NULL);
		} else {
			buf[strlen(buf) - 1] = '\0';

			if (desc->spec.values) {
				unsigned	i, j, found;

				/* multiple choice, map numeric input to values entry */
				if (sscanf(buf, "%u", &j) < 1) {
					printf("Invalid input: \"%s\"\n", buf);

					goto _next;
				}

				for (found = i = 0; desc->spec.values[i]; i++) {
					if (i == j) {
						til_settings_add_value(desc->container, desc->spec.key, desc->spec.values[i], NULL);
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
				til_settings_add_value(desc->container, desc->spec.key, buf, NULL);
			}
		}
_next:
		til_setting_desc_free(desc);
	}

	return r < 0 ? r : additions;
}
