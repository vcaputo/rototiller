AC_INIT([rototiller], [1.0], [vcaputo@pengaru.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_CONFIG_MACRO_DIRS([m4])
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AR
AM_SILENT_RULES([yes])
LT_INIT([disable-shared])

PKG_CHECK_MODULES(DRM, libdrm,
	AM_CONDITIONAL(ENABLE_DRM, declare build_rototiller=true)
	AC_DEFINE(HAVE_DRM, [1], [Define to 1 with drm present]),
	AM_CONDITIONAL(ENABLE_DRM, false)
)

PKG_CHECK_MODULES(SDL, sdl2,
	AM_CONDITIONAL(ENABLE_SDL, declare build_rototiller=true)
	AC_DEFINE(HAVE_SDL, [1], [Define to 1 with sdl2 present]),
	AM_CONDITIONAL(ENABLE_SDL, false)
)

AM_CONDITIONAL(ENABLE_ROTOTILLER, [test "x$build_rototiller" = xtrue])

LIBS="$DRM_LIBS $SDL_LIBS $LIBS"
CFLAGS="$DRM_CFLAGS $SDL_CFLAGS $CFLAGS -D_GNU_SOURCE -D_POSIX -Wall -Werror"

AX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$PTHREAD_CFLAGS $CFLAGS"
CC="$PTHREAD_CC"

AC_CONFIG_FILES([
 Makefile
 src/Makefile
 src/libs/Makefile
 src/libs/ascii/Makefile
 src/libs/grid/Makefile
 src/libs/din/Makefile
 src/libs/rocket/Makefile
 src/libs/puddle/Makefile
 src/libs/ray/Makefile
 src/libs/sig/Makefile
 src/libs/txt/Makefile
 src/modules/Makefile
 src/modules/asc/Makefile
 src/modules/blinds/Makefile
 src/modules/book/Makefile
 src/modules/checkers/Makefile
 src/modules/compose/Makefile
 src/modules/drizzle/Makefile
 src/modules/droste/Makefile
 src/modules/flow/Makefile
 src/modules/flui2d/Makefile
 src/modules/julia/Makefile
 src/modules/meta2d/Makefile
 src/modules/mixer/Makefile
 src/modules/moire/Makefile
 src/modules/montage/Makefile
 src/modules/pan/Makefile
 src/modules/pixbounce/Makefile
 src/modules/plasma/Makefile
 src/modules/plato/Makefile
 src/modules/playit/Makefile
 src/modules/ray/Makefile
 src/modules/rkt/Makefile
 src/modules/roto/Makefile
 src/modules/rtv/Makefile
 src/modules/shapes/Makefile
 src/modules/signals/Makefile
 src/modules/snow/Makefile
 src/modules/sparkler/Makefile
 src/modules/spiro/Makefile
 src/modules/spokes/Makefile
 src/modules/stars/Makefile
 src/modules/strobe/Makefile
 src/modules/stub/Makefile
 src/modules/submit/Makefile
 src/modules/swab/Makefile
 src/modules/swarm/Makefile
 src/modules/voronoi/Makefile
])

AC_CONFIG_SUBDIRS([
 src/libs/playit
])

AC_OUTPUT
