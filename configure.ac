AC_INIT([rototiller], [1.0], [vcaputo@pengaru.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_MACRO_DIRS([m4])
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AR
AC_PROG_RANLIB
AM_SILENT_RULES([yes])

PKG_CHECK_MODULES(DRM, libdrm,
	AM_CONDITIONAL(ENABLE_DRM, true)
	AC_DEFINE(HAVE_DRM, [1], [Define to 1 with drm present]),
	AM_CONDITIONAL(ENABLE_DRM, false)
)

LIBS="$DRM_LIBS $LIBS"
CFLAGS="$DRM_CFLAGS $CFLAGS"

AX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"

dnl Check for SDL
SDL_VERSION=2.0
AM_PATH_SDL2($SDL_VERSION,
	:,
	AC_MSG_ERROR([*** SDL version $SDL_VERSION not found!])
)

CFLAGS="$CFLAGS $SDL_CFLAGS"
LIBS="$LIBS $SDL_LIBS"

AC_CONFIG_FILES([
 Makefile
 src/Makefile
 src/libs/Makefile
 src/libs/ascii/Makefile
 src/libs/grid/Makefile
 src/libs/din/Makefile
 src/libs/puddle/Makefile
 src/libs/ray/Makefile
 src/libs/sig/Makefile
 src/libs/txt/Makefile
 src/modules/Makefile
 src/modules/compose/Makefile
 src/modules/drizzle/Makefile
 src/modules/flui2d/Makefile
 src/modules/julia/Makefile
 src/modules/meta2d/Makefile
 src/modules/montage/Makefile
 src/modules/pixbounce/Makefile
 src/modules/plasma/Makefile
 src/modules/plato/Makefile
 src/modules/ray/Makefile
 src/modules/roto/Makefile
 src/modules/rtv/Makefile
 src/modules/snow/Makefile
 src/modules/sparkler/Makefile
 src/modules/spiro/Makefile
 src/modules/stars/Makefile
 src/modules/submit/Makefile
 src/modules/swab/Makefile
])
AC_OUTPUT
