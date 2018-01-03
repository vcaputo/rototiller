AC_INIT([rototiller], [1.0], [vcaputo@pengaru.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_MACRO_DIRS([m4])
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AR
AC_PROG_RANLIB
AM_SILENT_RULES([yes])
PKG_CHECK_MODULES(ROTOTILLER, libdrm)

AX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"

dnl Check for SDL
SDL_VERSION=2.0.5
AM_PATH_SDL2($SDL_VERSION,
	:,
	AC_MSG_ERROR([*** SDL version $SDL_VERSION not found!])
)

CFLAGS="$CFLAGS $SDL_CFLAGS"
LIBS="$LIBS $SDL_LIBS"

AC_CONFIG_FILES([
 Makefile
 src/Makefile
 src/modules/Makefile
 src/modules/julia/Makefile
 src/modules/plasma/Makefile
 src/modules/ray/Makefile
 src/modules/roto/Makefile
 src/modules/sparkler/Makefile
 src/modules/stars/Makefile
])
AC_OUTPUT
