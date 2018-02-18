#include <assert.h>
#include <stdio.h>

#include "settings.h"
#include "util.h"

/* add key=value, but if key is NULL, use value as key. */
static int add_value(settings_t *settings, const char *key, const char *value)
{
	assert(settings);

	if (!key) {
		key = value;
		value = NULL;
	}

	assert(key);

	return settings_add_value(settings, key, value);
}


/* returns negative on error, otherwise number of additions made to settings */
int setup_interactively(settings_t *settings, int (*setup_func)(settings_t *settings, setting_desc_t **next), int defaults)
{
	unsigned	additions = 0;
	char		buf[256] = "\n";
	setting_desc_t	*next;
	int		r;

	assert(settings);
	assert(setup_func);

	/* TODO: regex and error handling */

	while ((r = setup_func(settings, &next)) > 0) {
		additions++;

		if (!defaults)
			puts("");

		if (next->values) {
			unsigned	i, preferred = 0;
			int		width = 0;

			for (i = 0; next->values[i]; i++) {
				int	len;

				len = strlen(next->values[i]);
				if (len > width)
					width = len;
			}

			/* multiple choice */
			if (!defaults)
				printf("Select %s:\n", next->name);

			for (i = 0; next->values[i]; i++) {
				if (!defaults)
					printf(" %u: %*s%s%s\n", i, width, next->values[i],
						next->annotations ? ": " : "",
						next->annotations ? next->annotations[i] : "");

				if (!strcmp(next->preferred, next->values[i]))
					preferred = i;
			}

			if (!defaults)
				printf("Enter a value 0-%u [%u (%s)]: ",
					i - 1, preferred, next->preferred);
		} else {
			/* arbitrarily typed input */
			if (!defaults)
				printf("%s [%s]: ", next->name, next->preferred);
		}

		if (!defaults) {
			fflush(stdout);
			fgets(buf, sizeof(buf), stdin);
		}

		if (*buf == '\n') {
			/* accept preferred */
			add_value(settings, next->key, next->preferred);
		} else {
			buf[strlen(buf) - 1] = '\0';

			if (next->values) {
				unsigned	i, j, found;

				/* multiple choice, map numeric input to values entry */
				if (sscanf(buf, "%u", &j) < 1) {
					printf("Invalid input: \"%s\"\n", buf);

					goto _next;
				}

				for (found = i = 0; next->values[i]; i++) {
					if (i == j) {
						add_value(settings, next->key, next->values[i]);
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
				add_value(settings, next->key, buf);
			}
		}

_next:
		setting_desc_free(next);
	}

	return r < 0 ? r : additions;
}
