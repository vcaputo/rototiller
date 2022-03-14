#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "til_args.h"

/*
 * ./rototiller --video=drm,dev=/dev/dri/card3,connector=VGA-1,mode=640x480@60
 * ./rototiller --video=sdl,size=640x480
 * ./rototiller --module=roto,foo=bar,module=settings
 * ./rototiller --defaults
 *
 * unrecognized arguments trigger an -EINVAL error, unless res_{argc,argv} are non-NULL
 * where a new argv will be allocated and populated with the otherwise invalid arguments
 * in the same order they were encountered in the input argv.  This is to support integration
 * with argv-handling application libraries like glib(g_application_run()).
 */
static int args_parse(int argc, const char *argv[], til_args_t *res_args, int *res_argc, const char **res_argv[])
{
	assert(argv);
	assert(res_args);
	assert(!res_argc || res_argv);

	if (res_argv) {
		*res_argv = calloc(argc + 1, sizeof(*res_argv));

		if (!*res_argv)
			return -ENOMEM;

		*res_argc = 0;
	}

	if (!argc)
		return 0;

	if (res_argv)
		(*res_argv)[(*res_argc)++] = argv[0];

	/* this is intentionally being kept very simple, no new dependencies like getopt. */

	for (int i = 1; i < argc; i++) {
		if (!strncmp("--video=", argv[i], 8)) {
			res_args->video = &argv[i][8];
		} else if (!strncmp("--module=", argv[i], 9)) {
			res_args->module = &argv[i][9];
		} else if (!strcmp("--defaults", argv[i])) {
			res_args->use_defaults = 1;
		} else if (!strcmp("--help", argv[i])) {
			res_args->help = 1;
		} else {
			if (!res_argv)
				return -EINVAL;

			(*res_argv)[(*res_argc)++] = argv[i];
		}
	}

	return 0;
}


int til_args_pruned_parse(int argc, const char *argv[], til_args_t *res_args, int *res_argc, const char **res_argv[])
{
	assert(res_argc && res_argv);

	return args_parse(argc, argv, res_args, res_argc, res_argv);
}


int til_args_parse(int argc, const char *argv[], til_args_t *res_args)
{
	return args_parse(argc, argv, res_args, NULL, NULL);

}


int til_args_help(FILE *out)
{
	return fprintf(out,
		"  --defaults	use defaults for unspecified settings\n"
		"  --help	this help\n"
		"  --module=	module settings\n"
		"  --video=	video settings\n"
		);
}
