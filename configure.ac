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

AC_CONFIG_FILES([
 Makefile
 src/Makefile
 src/modules/Makefile
 src/modules/ray/Makefile
 src/modules/roto/Makefile
 src/modules/sparkler/Makefile
 src/modules/stars/Makefile
])
AC_OUTPUT
