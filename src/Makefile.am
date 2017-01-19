SUBDIRS = modules
bin_PROGRAMS = rototiller
rototiller_SOURCES = drmsetup.c drmsetup.h fb.c fb.h fps.c fps.h rototiller.c rototiller.h util.c util.h
rototiller_LDADD = @ROTOTILLER_LIBS@ -lm modules/ray/libray.a modules/roto/libroto.a modules/sparkler/libsparkler.a modules/stars/libstars.a
rototiller_CPPFLAGS = @ROTOTILLER_CFLAGS@
