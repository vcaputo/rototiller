/*
 *  Copyright (C) 2023 - Vito Caputo - <vcaputo@pengaru.com>
 *
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _TIL_STREAM_H
#define _TIL_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "til_module_context.h"

typedef struct til_stream_t til_stream_t;
typedef struct til_stream_pipe_t til_stream_pipe_t;
typedef struct til_tap_t til_tap_t;

/* since pipe is opaque, pass all the member values too.  pipe is still given so
 * things can be done to it (like changing the ownership?)
 * return 0 to stop iterating, 1 to continue, -errno on error
 */
typedef int (til_stream_iter_func_t)(void *context, til_stream_pipe_t *pipe, const void *owner, const void *owner_foo, const til_tap_t *driving_tap);

/* this provides a way to intercept pipe creations/deletions when they occur,
 * allowing another module to snipe ownership when they appear and cleanup
 * its resources when they disappear.
 */
typedef struct til_stream_hooks_t {
	int (*pipe_ctor)(void *context, til_stream_t *stream, const void *owner, const void *owner_foo, const char *parent_path, uint32_t parent_hash, const til_tap_t *tap, const void **res_owner, const void **res_owner_foo, const til_tap_t **res_tap);	/* called immediately *before* pipe would be created by tap using these parameters, return <0 on error, 0 on unhandled by hook, 1 on handled with desired owner/owner_foo/tap stored in res_* */
	int (*pipe_dtor)(void *context, til_stream_t *stream, const void *owner, const void *owner_foo, const char *parent_path, const til_tap_t *tap);	/* called immediately *after* pipe "destroyed" (withdrawn from stream) */
} til_stream_hooks_t;

til_stream_t * til_stream_new(void);
til_stream_t * til_stream_free(til_stream_t *stream);
int til_stream_set_hooks(til_stream_t *stream, const til_stream_hooks_t *hooks, void *context);
int til_stream_unset_hooks(til_stream_t *stream, const til_stream_hooks_t *hooks);

/* bare interface for non-module-context owned taps */
int til_stream_tap(til_stream_t *stream, const void *owner, const void *owner_foo, const char *parent_path, uint32_t parent_hash, const til_tap_t *tap);

/* convenience helper for use within modules */
static inline int til_stream_tap_context(til_stream_t *stream, const til_module_context_t *module_context, const void *owner_foo, const til_tap_t *tap)
{
	return til_stream_tap(stream, module_context, owner_foo, module_context->path, module_context->path_hash, tap);
}

void til_stream_untap_owner(til_stream_t *stream, const void *owner);
void til_stream_fprint(til_stream_t *stream, FILE *out);

int til_stream_for_each_pipe(til_stream_t *stream, til_stream_iter_func_t pipe_cb, void *cb_arg);
void til_stream_pipe_set_owner(til_stream_pipe_t *pipe, const void *owner, const void *owner_foo);
void til_stream_pipe_set_driving_tap(til_stream_pipe_t *pipe, const til_tap_t *driving_tap);

#endif
