SUBDIRS = modules
bin_PROGRAMS = rototiller
rototiller_SOURCES = drmsetup.c drmsetup.h drm_fb.c drm_fb.h fb.c fb.h fps.c fps.h rototiller.c rototiller.h settings.h settings.c threads.c threads.h util.c util.h
rototiller_LDADD = @ROTOTILLER_LIBS@ -lm modules/julia/libjulia.a modules/plasma/libplasma.a modules/ray/libray.a modules/roto/libroto.a modules/sparkler/libsparkler.a modules/stars/libstars.a
rototiller_CPPFLAGS = @ROTOTILLER_CFLAGS@
