SUBDIRS = modules
bin_PROGRAMS = rototiller
rototiller_SOURCES = fb.c fb.h fps.c fps.h rototiller.c rototiller.h sdl_fb.c settings.h settings.c setup.h setup.c threads.c threads.h util.c util.h
if ENABLE_DRM
rototiller_SOURCES += drm_fb.c
endif
rototiller_LDADD = -lm modules/julia/libjulia.a modules/plasma/libplasma.a modules/ray/libray.a modules/roto/libroto.a modules/sparkler/libsparkler.a modules/stars/libstars.a
