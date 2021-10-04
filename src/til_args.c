#include <assert.h>
#include <errno.h>
#include <string.h>

#include "til_args.h"

/*
 * ./rototiller --video=drm,dev=/dev/dri/card3,connector=VGA-1,mode=640x480@60
 * ./rototiller --video=sdl,size=640x480
 * ./rototiller --module=roto,foo=bar,module=settings
 * ./rototiller --defaults
 */
int til_args_parse(int argc, const char *argv[], til_args_t *res_args)
{
	assert(argc > 0);
	assert(argv);
	assert(res_args);

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
			return -EINVAL;
		}
	}

	return 0;
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
