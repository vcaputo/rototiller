SUBDIRS = libs modules

noinst_LTLIBRARIES = libtil.la
libtil_la_SOURCES =						\
			til.c					\
			til.h					\
			til_args.c				\
			til_args.h				\
			til_audio.c				\
			til_audio.h				\
			til_audio_context.c			\
			til_audio_context.h			\
			til_builtins.c				\
			til_fb.c				\
			til_fb.h				\
			til_jenkins.c				\
			til_jenkins.h				\
			til_limits.h				\
			til_module_context.c			\
			til_module_context.h			\
			til_settings.c				\
			til_settings.h				\
			til_setup.c				\
			til_setup.h				\
			til_str.c				\
			til_str.h				\
			til_stream.c				\
			til_stream.h				\
			til_tap.h				\
			til_threads.c				\
			til_threads.h				\
			til_util.c				\
			til_util.h				\
			til_video_setup.h

libtil_la_CPPFLAGS = -I@top_srcdir@/src
libtil_la_LIBADD =						\
			modules/asc/libasc.la			\
			modules/blinds/libblinds.la		\
			modules/book/libbook.la			\
			modules/checkers/libcheckers.la		\
			modules/compose/libcompose.la		\
			modules/drizzle/libdrizzle.la		\
			modules/droste/libdroste.la		\
			modules/flow/libflow.la			\
			modules/flui2d/libflui2d.la		\
			modules/julia/libjulia.la		\
			modules/meta2d/libmeta2d.la		\
			modules/mixer/libmixer.la		\
			modules/moire/libmoire.la		\
			modules/montage/libmontage.la		\
			modules/pan/libpan.la			\
			modules/pixbounce/libpixbounce.la	\
			modules/plasma/libplasma.la		\
			modules/plato/libplato.la		\
			modules/playit/libplayit.la		\
			modules/ray/libray.la			\
			modules/rkt/librkt.la			\
			modules/roto/libroto.la			\
			modules/rtv/librtv.la			\
			modules/shapes/libshapes.la		\
			modules/signals/libsignals.la		\
			modules/snow/libsnow.la			\
			modules/sparkler/libsparkler.la		\
			modules/spiro/libspiro.la		\
			modules/spokes/libspokes.la		\
			modules/stars/libstars.la		\
			modules/strobe/libstrobe.la		\
			modules/stub/libstub.la			\
			modules/submit/libsubmit.la		\
			modules/swab/libswab.la			\
			modules/swarm/libswarm.la		\
			modules/voronoi/libvoronoi.la		\
			libs/grid/libgrid.la			\
			libs/puddle/libpuddle.la		\
			libs/ray/libray.la			\
			libs/rocket/librocket.la		\
			libs/sig/libsig.la			\
			libs/txt/libtxt.la			\
			libs/ascii/libascii.la			\
			libs/din/libdin.la

bin_PROGRAMS = rototiller
rototiller_SOURCES =						\
			fps.c					\
			fps.h					\
			main.c					\
			mem_audio.c				\
			mem_fb.c				\
			setup.c					\
			setup.h					\
			til.h					\
			til_fb.c				\
			til_fb.h				\
			til_settings.c				\
			til_settings.h				\
			til_threads.c				\
			til_threads.h				\
			til_util.c				\
			til_util.h

if ENABLE_SDL
rototiller_SOURCES += sdl_audio.c sdl_fb.c
endif
if ENABLE_DRM
rototiller_SOURCES += drm_fb.c
endif
rototiller_LDADD = libtil.la @top_builddir@/src/libs/playit/src/libplayit.a -lm
