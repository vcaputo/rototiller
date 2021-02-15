SUBDIRS = libs modules

noinst_LTLIBRARIES = librototiller.la
librototiller_la_SOURCES = fb.c fb.h knobs.h rototiller.c rototiller.h settings.h settings.c threads.c threads.h util.c util.h
librototiller_la_CPPFLAGS = -I@top_srcdir@/src
librototiller_la_LIBADD = modules/compose/libcompose.la modules/drizzle/libdrizzle.la modules/flui2d/libflui2d.la modules/julia/libjulia.la modules/meta2d/libmeta2d.la modules/montage/libmontage.la modules/pixbounce/libpixbounce.la modules/plasma/libplasma.la modules/plato/libplato.la modules/ray/libray.la modules/roto/libroto.la modules/rtv/librtv.la modules/snow/libsnow.la modules/sparkler/libsparkler.la modules/spiro/libspiro.la modules/stars/libstars.la modules/submit/libsubmit.la modules/swab/libswab.la modules/swarm/libswarm.la libs/grid/libgrid.la libs/puddle/libpuddle.la libs/ray/libray.la libs/sig/libsig.la libs/txt/libtxt.la libs/ascii/libascii.la libs/din/libdin.la

bin_PROGRAMS = rototiller
rototiller_SOURCES = fb.c fb.h fps.c fps.h knobs.h main.c rototiller.h sdl_fb.c settings.h settings.c setup.h setup.c threads.c threads.h util.c util.h
if ENABLE_DRM
rototiller_SOURCES += drm_fb.c
endif
rototiller_LDADD = librototiller.la -lm
#rototiller_LDADD = modules/compose/libcompose.a modules/drizzle/libdrizzle.a modules/flui2d/libflui2d.a modules/julia/libjulia.a modules/meta2d/libmeta2d.a modules/montage/libmontage.a modules/pixbounce/libpixbounce.a modules/plasma/libplasma.a modules/plato/libplato.a modules/ray/libray.a modules/roto/libroto.a modules/rtv/librtv.a modules/snow/libsnow.a modules/sparkler/libsparkler.a modules/spiro/libspiro.a modules/stars/libstars.a modules/submit/libsubmit.a modules/swab/libswab.a modules/swarm/libswarm.a libs/grid/libgrid.a libs/puddle/libpuddle.a libs/ray/libray.a libs/sig/libsig.a libs/txt/libtxt.a libs/ascii/libascii.a libs/din/libdin.a -lm
