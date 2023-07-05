#ifndef _RKT_H
#define _RKT_H

#include "til.h"
#include "til_module_context.h"

typedef struct rkt_scene_t {
	til_module_context_t	*module_ctxt;
} rkt_scene_t;

typedef struct rkt_context_t {
	til_module_context_t	til_module_context;

	struct sync_device	*sync_device;
	const struct sync_track	*scene_track;
	double			rows_per_ms;
	double			rocket_row;
	unsigned		last_ticks;
	unsigned		paused:1;
	size_t			n_scenes;
	rkt_scene_t		*scenes;
} rkt_context_t;

typedef struct rkt_setup_scene_t {
	const til_module_t	*module;
	til_setup_t		*setup;			/* Baked setup as-configured via setup. */
} rkt_setup_scene_t;

typedef struct rkt_setup_t {
	til_setup_t		til_setup;
	const char		*base;
	double			rows_per_ms;
	unsigned		connect:1;
	const char		*host;
	unsigned short		port;
	size_t			n_scenes;
	rkt_setup_scene_t	scenes[];
} rkt_setup_t;

#endif
