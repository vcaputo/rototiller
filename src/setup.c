#include <assert.h>
#include <stdio.h>

#include "setup.h"
#include "til_settings.h"
#include "til_setup.h"
#include "til_str.h"
#include "til_util.h"


/* Helper for turning the failed desc into a char * and storing it in the provided place before propagating
 * the error return value
 */
static int setup_ret_failed_desc_path(const til_setting_desc_t *failed_desc, int r, const char **res_failed_desc_path)
{
	til_str_t	*str;

	assert(res_failed_desc_path);

	str = til_str_new("");
	if (!str)
		return r;

	if (til_setting_desc_strprint_path(failed_desc, str) < 0)
		return r;

	*res_failed_desc_path = til_str_to_buf(str, NULL);

	return r;
}


/* returns negative on error, otherwise number of additions made to settings */
int setup_interactively(til_settings_t *settings, int (*setup_func)(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup), int defaults, til_setup_t **res_setup, const char **res_failed_desc_path)
{
	unsigned			additions = 0;
	char				buf[256] = "\n";
	til_setting_t			*setting;
	const til_setting_desc_t	*desc;
	int				r;

	assert(settings);
	assert(setup_func);

	/* TODO: regex and error handling */

	/* until all the setup_funcs guarantee they return the failed setting on -EINVAL w/non-NULL res_setup (finalizing),
	 * this will be done in two steps; this first loop just constructs the settings heirarchy, and if it fails with
	 * -EINVAL we will use the setting for logging.  Then after this loop, one last recurrence of setup_func() w/res_setup
	 * actually set.  Once all setup_funcs behave well even in res_setup we'll go back to just the loop on setup_func().
	 */
	while ((r = setup_func(settings, &setting, &desc, NULL)) > 0) {
		assert(desc);

		additions++;

		/* if setup_func() has returned a description for an undescribed preexisting setting,
		 * validate its value against the description and assign the description if it passes.
		 */
		if (setting && !setting->desc) {
			/* Apply override before, or after the spec_check()? unclear.
			 * TODO This probably also needs to move into a til_settings helper
			 */
			if (desc->spec.override) {
				const char	*o;

				o = desc->spec.override(setting->value);
				if (!o)
					return -ENOMEM;

				if (o != setting->value) {
					r = til_setting_set_raw_value(setting, o);
					free((void *)o);
					if (r < 0)
						return -ENOMEM;
				}
			}

			r = til_setting_check_spec(setting, &desc->spec);
			if (r < 0)
				return setup_ret_failed_desc_path(desc, r, res_failed_desc_path);

			if (desc->spec.as_nested_settings && !setting->value_as_nested_settings) {
				char	*label = NULL;

				if (!desc->spec.key) {
					/* generate a positional label for bare-value specs */
					r = til_settings_label_setting(desc->container, setting, &label);
					if (r < 0)
						return r;
				}

				setting->value_as_nested_settings = til_settings_new(NULL, desc->container, desc->spec.key ? : label, til_setting_get_raw_value(setting));
				free(label);

				/* FIXME: til_settings_new() seems like it should return an errno, since it can encounter parse errors too? */
				if (!setting->value_as_nested_settings)
					return setup_ret_failed_desc_path(desc, -ENOMEM, res_failed_desc_path);
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
			if (!defaults) {
				til_setting_desc_fprint_path(desc, stdout);
				printf(":\n %s:\n", desc->spec.name);
			}

			for (i = 0; desc->spec.values[i]; i++) {
				if (!defaults)
					printf(" %2u: %*s%s%s\n", i, width, desc->spec.values[i],
						desc->spec.annotations ? ": " : "",
						desc->spec.annotations ? desc->spec.annotations[i] : "");

				if (!strcasecmp(desc->spec.preferred, desc->spec.values[i]))
					preferred = i;
			}

			if (!defaults)
				printf(" Enter a value 0-%u [%u (%s)]: ",
					i - 1, preferred, desc->spec.preferred);
		} else {
			/* arbitrarily typed input */
			if (!defaults) {
				til_setting_desc_fprint_path(desc, stdout);
				printf(":\n %s [%s]: ", desc->spec.name, desc->spec.preferred);
			}
		}

		if (!defaults) {
			fflush(stdout);

			if (feof(stdin))
				return -EIO;

			fgets(buf, sizeof(buf), stdin);
		}

		if (*buf == '\n') {
			/* accept preferred */
			til_settings_add_value(desc->container, desc->spec.key, desc->spec.preferred);
		} else {
			buf[strlen(buf) - 1] = '\0';

			if (desc->spec.values && buf[0] != ':') {
				unsigned	i, j, found;

				/* multiple choice, map numeric input to values entry */
				if (sscanf(buf, "%u", &j) < 1) {
					printf("Invalid input: \"%s\"\n", buf);

					goto _next;
				}

				for (found = i = 0; desc->spec.values[i]; i++) {
					if (i == j) {
						til_settings_add_value(desc->container, desc->spec.key, desc->spec.values[i]);
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
				til_settings_add_value(desc->container, desc->spec.key, buf);
			}
		}
_next:
		til_setting_desc_free(desc);
	}

	if (r < 0) {
		if (r == -EINVAL)
			return setup_ret_failed_desc_path(setting->desc, r, res_failed_desc_path);

		return r;
	}

	r = setup_func(settings, &setting, &desc, res_setup);

	return r < 0 ? r : additions;
}
