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
typedef struct til_tap_t til_tap_t;

til_stream_t * til_stream_new(void);
til_stream_t * til_stream_free(til_stream_t *stream);

/* bare interface for non-module-context owned taps */
int til_stream_tap(til_stream_t *stream, const void *owner, const void *owner_foo, const char *parent_path, uint32_t parent_hash, const til_tap_t *tap);

/* convenience helper for use within modules */
static inline int til_stream_tap_context(til_stream_t *stream, const til_module_context_t *module_context, const void *owner_foo, const til_tap_t *tap)
{
	return til_stream_tap(stream, module_context, owner_foo, module_context->path, module_context->path_hash, tap);
}

void til_stream_untap_owner(til_stream_t *stream, const void *owner);
void til_stream_fprint(til_stream_t *stream, FILE *out);

#endif
