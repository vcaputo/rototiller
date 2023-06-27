#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "til_str.h"
#include "til_util.h"

/* This implements very rudimentary growable strings, and hasn't been optimized at all.
 *
 * The impetus for adding this is to get rid of the open_memstream() usage which is mostly
 * just being used as a convenient way to build up a buffer from format strings.  So this
 * basically implements just that ability using variadic functions...
 *
 * Why no open_memstream()?  Because Windows, that's why.  For some reason even mingw doesn't
 * have open_memstream().  FILE* I keep forgetting that you're dead to me.
 */

struct til_str_t {
	struct {
		size_t	allocated, used; /* used is length, including '\0' terminator */
	} size;
	char	*buf;
};


#define TIL_STR_MIN_SIZE	64


/* alloc always returns a buf w/nul terminator present */
static til_str_t * til_str_nulstr(size_t minsize)
{
	til_str_t	*str;

	str = calloc(1, sizeof(*str));
	if (!str)
		return NULL;

	str->size.used = 1;
	str->size.allocated = MAX(minsize, TIL_STR_MIN_SIZE);

	str->buf = calloc(1, str->size.allocated);
	if (!str->buf) {
		free(str);
		return NULL;
	}

	return str;
}


/* allocate a new til_str, starting with a dup of seed, just use "" for an empty str, there is no NULL str */
til_str_t * til_str_new(const char *seed)
{
	til_str_t	*str;
	size_t		len;

	assert(seed);

	len = strlen(seed);
	str = til_str_nulstr(len + 1);
	if (!str)
		return NULL;

	memcpy(str->buf, seed, len);
	str->size.used += len;

	return str;
}


void * til_str_free(til_str_t *str)
{
	if (str) {
		free(str->buf);
		free(str);
	}

	return NULL;
}


/* allocate a new til_str from a fmt string + args */
til_str_t * til_str_newf(const char *format, ...)
{
	til_str_t	*str;
	va_list		ap;

	assert(format);


	va_start(ap, format);
	str = til_str_nulstr(vsnprintf(NULL, 0, format, ap) + 1);
	va_end(ap);
	if (!str)
		return NULL;

	va_start(ap, format);
	str->size.used += vsnprintf(str->buf, str->size.allocated, format, ap);
	va_end(ap);

	assert(str->size.used <= str->size.allocated);

	return str;
}


/* append to an existing til_str_t from a fmt string + args */
int til_str_appendf(til_str_t *str, const char *format, ...)
{
	size_t	len;
	va_list	ap;

	assert(str);
	assert(format);

	va_start(ap, format);
	len = vsnprintf(NULL, 0, format, ap);
	va_end(ap);

	if (str->size.used + len > str->size.allocated) {
		char	*new;

		new = realloc(str->buf, str->size.used + len);
		if (!new)
			return -ENOMEM;

		str->buf = new;
		str->size.allocated = str->size.used + len;
	}

	va_start(ap, format);
	str->size.used += vsnprintf(&str->buf[str->size.used - 1],
				    str->size.allocated - (str->size.used - 1),
				    format, ap);
	va_end(ap);

	assert(str->size.used <= str->size.allocated);

	return 0;
}


/* strdup() the /used/ contents of str */
char * til_str_strdup(const til_str_t *str)
{
	assert(str);

	return strdup(str->buf);
}


/* a valid \0-terminated string is _always_ maintained @ str->buf so callers can just use it as a string..
 * but must not hang onto that pointer across more til_str() calls on the same str.
 * The length (excluding the \0) is returned in res_len if non-NULL
 */
const char * til_str_buf(const til_str_t *str, size_t *res_len)
{
	assert(str);

	if (res_len)
		*res_len = str->size.used - 1;

	return str->buf;
}


/* for when you just want the buf without the dup overhead, and don't need the str anymore */
char * til_str_to_buf(til_str_t *str, size_t *res_len)
{
	char	*buf;

	assert(str);

	if (res_len)
		*res_len = str->size.used - 1;

	buf = str->buf;
	free(str);

	return buf;
}


/* truncate off trailing \n or \r\n if present
 * str is passed through for convenience (til_str_to_buf(til_str_chomp(str), &len)) etc...
 */
til_str_t * til_str_chomp(til_str_t *str)
{
	assert(str);

	if (str->size.used > 1 && str->buf[str->size.used - 2] == '\n') {
		if (str->size.used > 2 && str->buf[str->size.used - 3] == '\r')
			str->size.used--;
		str->size.used--;
		str->buf[str->size.used - 1] = '\0';
	}

	return str;
}
