SUBDIRS = libs modules
bin_PROGRAMS = rototiller
rototiller_SOURCES = fb.c fb.h fps.c fps.h rototiller.c rototiller.h sdl_fb.c settings.h settings.c setup.h setup.c threads.c threads.h util.c util.h
if ENABLE_DRM
rototiller_SOURCES += drm_fb.c
endif
rototiller_LDADD = modules/flui2d/libflui2d.a modules/julia/libjulia.a modules/meta2d/libmeta2d.a modules/montage/libmontage.a modules/pixbounce/libpixbounce.a modules/plasma/libplasma.a modules/ray/libray.a modules/roto/libroto.a modules/rtv/librtv.a modules/snow/libsnow.a modules/sparkler/libsparkler.a modules/stars/libstars.a modules/submit/libsubmit.a modules/swab/libswab.a libs/grid/libgrid.a libs/ray/libray.a libs/txt/libtxt.a libs/ascii/libascii.a libs/din/libdin.a -lm
