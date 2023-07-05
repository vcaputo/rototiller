#ifndef _RKT_H
#define _RKT_H

#include "til.h"
#include "til_module_context.h"
#include "til_settings.h"

typedef struct rkt_scener_t rkt_scener_t;

typedef struct rkt_scene_t {
	til_module_context_t	*module_ctxt;
} rkt_scene_t;

typedef struct rkt_context_t {
	til_module_context_t	til_module_context;

	rkt_scener_t		*scener;
	struct sync_device	*sync_device;
	const struct sync_track	*scene_track;
	double			rows_per_ms;
	double			rocket_row;
	unsigned		last_ticks;
	unsigned		paused:1;
	size_t			n_scenes;
	rkt_scene_t		*scenes;
	unsigned		scene;		/* current scene (usually driven by the scene track data,
						 * but scener may override it to force showing a specific scene)
						 */
} rkt_context_t;

typedef struct rkt_setup_scene_t {
	const til_module_t	*module;
	til_setup_t		*setup;		/* Baked setup as-configured via setup. */
} rkt_setup_scene_t;

typedef struct rkt_setup_t {
	til_setup_t		til_setup;
	til_settings_t		*settings;	/* Settings instance used to produce rkt's root setup,
						 * which rkt grabs a reference to for serializing its
						 * entirety "as args".  The per-scene setups also grab
						 * reference to their respective settings instances, for
						 * editability within their levels of the rkt settings
						 * heirarchy.
						 */
	til_settings_t		*scenes_settings;

	const char		*base;
	double			rows_per_ms;
	unsigned		connect:1;
	unsigned		scener_listen:1;
	const char		*host, *scener_address;
	unsigned short		port, scener_port;
	size_t			n_scenes;
	rkt_setup_scene_t	scenes[];
} rkt_setup_t;

#endif
