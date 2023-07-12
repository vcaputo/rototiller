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

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "til.h"
#include "til_jenkins.h"
#include "til_stream.h"
#include "til_tap.h"

/* A stream in libtil is basically a hash table for tracking dynamic
 * information for modules to create/modify/access.  The objects stored in the
 * table are "stream pipes" and endpoints called taps, making this something
 * like a miniature in-memory implementation of named pipes, conceptually
 * anyways (there are no actual file descriptors).
 */

#if 0
/* example usage: */
typedef struct foo_t {
	struct {
		til_tap_t	position;
	} taps;
	struct {
		v2f_t		position;
	} vars;

	v2f_t	*position;
} foo_t;

foo_t * foo_create_context(void)
{
	foo_t	*foo = malloc(sizeof(foo_t));

	/* This creates an isolated (pipe-)tap binding our local position variable and pointer
	 * to a name for later "tapping" onto a stream.
	 */
	foo->taps.position = til_tap_init_v2f(&foo->position, 1, &foo->vars.position, "position");
}

foo_render(foo_t *foo, til_fb_fragment_t *fragment)
{
	if (!til_stream_tap_context(fragment->stream, foo, &foo->taps.position)) {
		/* got nothing, we're driving position */
		foo->position->x = cosf(ticks);
		foo->position->y = sinf(ticks);
	} /* else { got something, just use foo->position as-is */
}
#endif


#define TIL_STREAM_PIPE_BUCKETS_COUNT	256
#define TIL_STREAM_CTXT_BUCKETS_COUNT	64


struct til_stream_pipe_t {
	til_stream_pipe_t	*next;
	const void		*owner;		/* for untap_owner() differentiation */
	const void		*owner_foo;	/* supplemental pointer for owner's use */
	char			*parent_path;
	const til_tap_t		*driving_tap;	/* tap producing values for the pipe */
	uint32_t		hash;		/* hash of (driving_tap->name_hash ^ .parent_hash) */
};

struct til_stream_module_context_t {
	til_stream_module_context_t	*next;
	uint32_t			path_hash;

	size_t				n_module_contexts;
	til_module_context_t		*module_contexts[];
};

typedef struct til_stream_t {
	pthread_mutex_t			mutex;
	volatile int			ended;
	const til_stream_hooks_t	*hooks;
	void				*hooks_context;
	til_stream_pipe_t		*pipe_buckets[TIL_STREAM_PIPE_BUCKETS_COUNT];
	til_stream_module_context_t	*module_context_buckets[TIL_STREAM_CTXT_BUCKETS_COUNT];
} til_stream_t;


til_stream_t * til_stream_new(void)
{
	til_stream_t	*stream;

	stream = calloc(1, sizeof(til_stream_t));
	if (!stream)
		return NULL;

	pthread_mutex_init(&stream->mutex, NULL);

	return stream;
}


void til_stream_end(til_stream_t *stream)
{
	assert(stream);

	stream->ended = 1;
}


int til_stream_active(til_stream_t *stream)
{
	assert(stream);

	return !stream->ended;
}


til_stream_t * til_stream_free(til_stream_t *stream)
{
	if (!stream)
		return NULL;

	for (int i = 0; i < TIL_STREAM_PIPE_BUCKETS_COUNT; i++) {
		for (til_stream_pipe_t *p = stream->pipe_buckets[i], *p_next; p != NULL; p = p_next) {
			p_next = p->next;
			free(p->parent_path);
			free(p);
		}
	}

	pthread_mutex_destroy(&stream->mutex);
	free(stream);

	return NULL;
}


/* hooks are presently implemented as a singleton per-stream, I could imagine a stack
 * form where multiple triggers filter through until one of the callbacks
 * claim to have processed the trigger.  But let's just keep it stupid simple for now.
 *
 * this function is idempotent in the sense that a caller can keep resetting its hooks
 * when it's succeeded as becoming the owner of the hooks (by being first to set them on
 * a stream).  Only when different set of hooks are attempted to be set on a stream already
 * having hooks will it error out, to change hooks the caller must first unset them with the
 * matching hooks pointer.  There's no way to read the hooks out by a caller.  This is a weak
 * defensive mechanism to prevent multiple modules silently fighting over the hooks.  As long
 * as they're checking the return code they can say meaningful things about failing to set the
 * hooks... and it's assumed it'd be a flawed composition attempting to use multiple hook-setting
 * modules on the same stream (or program bug).
 */
int til_stream_set_hooks(til_stream_t *stream, const til_stream_hooks_t *hooks, void *context)
{
	assert(stream);
	assert(hooks);

	if (stream->hooks && stream->hooks != hooks)
		return -EEXIST;

	stream->hooks = hooks;
	stream->hooks_context = context;

	return 0;
}


int til_stream_unset_hooks(til_stream_t *stream, const til_stream_hooks_t *hooks)
{
	assert(stream);
	assert(hooks);

	if (stream->hooks && stream->hooks != hooks)
		return -EINVAL;

	stream->hooks = stream->hooks_context = NULL;

	return 0;
}


/* Taps the key-named type-typed pipe on the supplied stream.
 * If this is the first use of the pipe on this stream, new pipe will be created.
 * If the pipe exists on this stream, and the type/n_elems match, existing pipe will be used as-is.
 * If the pipe exists on this stream, but the type/n_elems mismatch, it's a program error and asserted
 *
 * -errno is returned on error, 0 when tap is driving, 1 when tap is passenger.
 *
 * If stream is NULL it's treated as if the key doesn't exist without a pipe creation.
 */
int til_stream_tap(til_stream_t *stream, const void *owner, const void *owner_foo, const char *parent_path, uint32_t parent_hash, const til_tap_t *tap)
{
	uint32_t		hash, bucket;
	til_stream_pipe_t	*pipe;
	const void		**p_owner = &owner, **p_owner_foo = &owner_foo;
	const til_tap_t		**p_tap = &tap;

	assert(tap);

	if (!stream) {
		*(tap->ptr) = tap->elems;

		return 0;
	}

	pthread_mutex_lock(&stream->mutex);

	hash = (tap->name_hash ^ parent_hash);
	bucket = hash % TIL_STREAM_PIPE_BUCKETS_COUNT;
	for (pipe = stream->pipe_buckets[bucket]; pipe != NULL; pipe = pipe->next) {
		if (pipe->hash == hash) {
			if (pipe->driving_tap == tap) {
				/* this is the pipe and we're driving */
				*(tap->ptr) = pipe->driving_tap->elems;

				pthread_mutex_unlock(&stream->mutex);
				return 0;
			}

			if (pipe->driving_tap->elems == *(tap->ptr) ||
			    (!strcmp(pipe->driving_tap->name, tap->name) && !strcmp(pipe->parent_path, parent_path))) {
				if (pipe->driving_tap->type != tap->type ||
				    pipe->driving_tap->n_elems != tap->n_elems) {
					assert(0); /* I don't really want to handle such errors at runtime */
					return -1;
				}

				/* this looks to be the pipe, but we're not driving, should we be? */
				if (pipe->driving_tap->inactive)
					pipe->driving_tap = tap;

				*(tap->ptr) = pipe->driving_tap->elems;

				pthread_mutex_unlock(&stream->mutex);
				return (tap != pipe->driving_tap);
			}
		}
	}

	if (stream->hooks && stream->hooks->pipe_ctor) {
		int	r;

		r = stream->hooks->pipe_ctor(stream->hooks_context, stream, owner, owner_foo, parent_path, parent_hash, tap, p_owner, p_owner_foo, p_tap);
		if (r < 0)
			return r;
	}

	/* matching pipe not found, create new one with tap as driver */
	pipe = calloc(1, sizeof(til_stream_pipe_t));
	if (!pipe) {
		pthread_mutex_unlock(&stream->mutex);
		return -ENOMEM;
	}

	pipe->owner = *p_owner;
	pipe->owner_foo = *p_owner_foo;
	pipe->driving_tap = *p_tap;

	pipe->parent_path = strdup(parent_path);
	if (!pipe->parent_path) {
		free(pipe);

		pthread_mutex_unlock(&stream->mutex);
		return -ENOMEM;
	}

	pipe->hash = hash;
	pipe->next = stream->pipe_buckets[bucket];
	stream->pipe_buckets[bucket] = pipe;

	pthread_mutex_unlock(&stream->mutex);
	return 0;
}


/* remove all pipes belonging to owner in stream, including pipes whose driving_tap is owned by owner */
void til_stream_untap_owner(til_stream_t *stream, const void *owner)
{
	for (int i = 0; i < TIL_STREAM_PIPE_BUCKETS_COUNT; i++) {
		for (til_stream_pipe_t *p = stream->pipe_buckets[i], *p_next, *p_prev = NULL; p != NULL; p = p_next) {
			p_next = p->next;

			if (p->owner == owner || p->driving_tap->owner == owner) {
				if (p == stream->pipe_buckets[i])
					stream->pipe_buckets[i] = p_next;
				else
					p_prev->next = p_next;

				if (stream->hooks && stream->hooks->pipe_dtor)
					stream->hooks->pipe_dtor(stream->hooks_context, stream, p->owner, p->owner_foo, p->parent_path, p->driving_tap);

				free(p);
			} else
				p_prev = p;
		}
	}
}


/* We need the higher-order types defined in order to print their contents.
 * libtil should probably just formally define these smoewhere for modules to
 * make use of.  Until now it's been very deliberate to try leave modules to be
 * relatively self-contained, even if they often reinvent the wheel as a
 * result... it's relatively harmless for small functionalities while keeping
 * the listings easier to grok as a whole esp. for a newcomer who isn't
 * necessarily comfortable jumping around a sprawling tree of files.
 */
typedef struct v2f_t { float	x, y; } v2f_t;

typedef struct v3f_t {
	float	x, y, z;
} v3f_t;

typedef struct v4f_t {
	float	x, y, z, w;
} v4f_t;


static int til_stream_fprint_pipe_cb(void *arg, til_stream_pipe_t *pipe, const void *owner, const void *owner_foo, const til_tap_t *driving_tap)
{
	FILE	*out = arg;

	fprintf(out, " %s/%s: ", pipe->parent_path, pipe->driving_tap->name);

	for (size_t j = 0; j < pipe->driving_tap->n_elems; j++) {
		const char	*sep = j ? ", " : "";

		switch (pipe->driving_tap->type) {
		case TIL_TAP_TYPE_I8:
			fprintf(out, "%"PRIi8"%s",
				*(*((int8_t **)pipe->driving_tap->ptr)),
				sep);
			break;

		case TIL_TAP_TYPE_I16:
			fprintf(out, "%"PRIi16"%s",
				*(*((int16_t **)pipe->driving_tap->ptr)),
				sep);
			break;

		case TIL_TAP_TYPE_I32:
			fprintf(out, "%"PRIi32"%s",
				*(*((int32_t **)pipe->driving_tap->ptr)),
				sep);
			break;

		case TIL_TAP_TYPE_I64:
			fprintf(out, "%"PRIi64"%s",
				*(*((int64_t **)pipe->driving_tap->ptr)),
				sep);
			break;

		case TIL_TAP_TYPE_U8:
			fprintf(out, "%"PRIu8"%s",
				*(*((int8_t **)pipe->driving_tap->ptr)),
				sep);
			break;

		case TIL_TAP_TYPE_U16:
			fprintf(out, "%"PRIu16"%s",
				*(*((int16_t **)pipe->driving_tap->ptr)),
				sep);
			break;

		case TIL_TAP_TYPE_U32:
			fprintf(out, "%"PRIu32"%s",
				*(*((int32_t **)pipe->driving_tap->ptr)),
				sep);
			break;

		case TIL_TAP_TYPE_U64:
			fprintf(out, "%"PRIu64"%s",
				*(*((int64_t **)pipe->driving_tap->ptr)),
				sep);
			break;

		case TIL_TAP_TYPE_FLOAT:
			fprintf(out, "%f%s",
				*(*((float **)pipe->driving_tap->ptr)),
				sep);
			break;

		case TIL_TAP_TYPE_DOUBLE:
			fprintf(out, "%f%s",
				*(*((double **)pipe->driving_tap->ptr)),
				sep);
			break;

		case TIL_TAP_TYPE_V2F:
			fprintf(out, "{%f,%f}%s",
				(*((v2f_t **)pipe->driving_tap->ptr))->x,
				(*((v2f_t **)pipe->driving_tap->ptr))->y,
				sep);
			break;

		case TIL_TAP_TYPE_V3F:
			fprintf(out, "{%f,%f,%f}%s",
				(*((v3f_t **)pipe->driving_tap->ptr))->x,
				(*((v3f_t **)pipe->driving_tap->ptr))->y,
				(*((v3f_t **)pipe->driving_tap->ptr))->z,
				sep);
			break;

		case TIL_TAP_TYPE_V4F:
			fprintf(out, "{%f,%f,%f,%f}%s",
				(*((v4f_t **)pipe->driving_tap->ptr))->x,
				(*((v4f_t **)pipe->driving_tap->ptr))->y,
				(*((v4f_t **)pipe->driving_tap->ptr))->z,
				(*((v4f_t **)pipe->driving_tap->ptr))->w,
				sep);
			break;

		case TIL_TAP_TYPE_M4F:
			fprintf(out, "M4F TODO%s", sep);
			break;

		case TIL_TAP_TYPE_VOIDP:
			fprintf(out, "%p%s", *((void **)pipe->driving_tap->ptr), sep);
			break;

		default:
			assert(0);
		}
		fprintf(out, "\n");
	}

	return 1;
}


/* XXX: note that while yes, this does acquire stream->mutex to serialize access to the table/pipes,
 * this mutex does not serialize access to the tapped variables.  So if this print is performed
 * during the threaded rendering phase of things, it will technically be racy.  The only strictly
 * correct place to perform this print race-free is in the rendering loop between submissions to
 * the rendering threads.  Arguably if careful about only printing while serial, there's no need
 * to acquire the mutex - but it's also an uncontended lock if that's the case so just take the
 * mutex anyways since we're accessing the underlying structure it protects.
 */
void til_stream_fprint_pipes(til_stream_t *stream, FILE *out)
{
	fprintf(out, "Pipes on stream %p:\n", stream);
	(void) til_stream_for_each_pipe(stream, til_stream_fprint_pipe_cb, out);
	fprintf(out, "\n");
}


/* returns -errno on error (from pipe_cb), 0 otherwise */
int til_stream_for_each_pipe(til_stream_t *stream, til_stream_pipe_iter_func_t pipe_cb, void *cb_context)
{
	assert(stream);
	assert(pipe_cb);

	pthread_mutex_lock(&stream->mutex);

	for (int i = 0; i < TIL_STREAM_PIPE_BUCKETS_COUNT; i++) {
		for (til_stream_pipe_t *p = stream->pipe_buckets[i]; p != NULL; p = p->next) {
			int	r;

			r = pipe_cb(cb_context, p, p->owner, p->owner_foo, p->driving_tap);
			if (r < 0) {
				pthread_mutex_unlock(&stream->mutex);
				return r;
			}
		}
	}

	pthread_mutex_unlock(&stream->mutex);
	return 0;
}


void til_stream_pipe_set_owner(til_stream_pipe_t *pipe, const void *owner, const void *owner_foo)
{
	assert(pipe);

	pipe->owner = owner;
	pipe->owner_foo = owner_foo;
}


/* NULLing out the driving_tap isn't supported, since the tap name is part of the pipe's identity,
 * just set tap.inactive to indicate another tap should take over driving.
 */
void til_stream_pipe_set_driving_tap(til_stream_pipe_t *pipe, const til_tap_t *driving_tap)
{
	assert(pipe);
	assert(driving_tap);

	pipe->driving_tap = driving_tap;
}


int til_stream_for_each_module_context(til_stream_t *stream, til_stream_module_context_iter_func_t module_context_cb, void *cb_arg)
{
	assert(stream);
	assert(module_context_cb);

	for (int i = 0; i < TIL_STREAM_CTXT_BUCKETS_COUNT; i++) {
		for (til_stream_module_context_t *c = stream->module_context_buckets[i]; c != NULL; c = c->next) {
			int	r;

			r = module_context_cb(cb_arg, c, c->n_module_contexts, (const til_module_context_t **)c->module_contexts);
			if (r < 0)
				return r;
		}
	}

	return 0;
}


int til_stream_register_module_contexts(til_stream_t *stream, size_t n_contexts, til_module_context_t **contexts)
{
	uint32_t			path_hash, bucket;
	til_stream_module_context_t	*c, *c_next, *c_prev;

	assert(stream);
	assert(n_contexts > 0);
	assert(contexts);

	path_hash = contexts[0]->setup->path_hash;
	bucket = path_hash % TIL_STREAM_CTXT_BUCKETS_COUNT;
	for (c = stream->module_context_buckets[bucket], c_prev = NULL; c != NULL; c = c_next) {
		c_next = c->next;

		if (c->path_hash != path_hash ||
		    strcmp(contexts[0]->setup->path, c->module_contexts[0]->setup->path)) {
			c_prev = c;
			continue;
		}

		/* due to the current approach of async gc for on-stream contexts, collisions at
		 * the same path are to be expected - and we just replace what's there.  In the
		 * future there will likely be a context invalidation api for marking what's replaced
		 * as stale, so the next time that reference gets used it's refreshed via the stream... TODO
		 */

		if (c_prev)
			c_prev->next = c_next;
		else
			stream->module_context_buckets[bucket] = c_next;

		/* we're only potentially reusing the stream_module_context_t container, the actual contexts are unreferenced */
		for (size_t i = 0; i < c->n_module_contexts; i++)
			c->module_contexts[i] = til_module_context_free((til_module_context_t *)c->module_contexts[i]);

		/* not big enough, FIXME TODO I think the context sets @path are being deprecated */
		if (c->n_module_contexts < n_contexts) {
			free(c);
			c = NULL;
		}

		break;
	}

	if (!c) {
		c = calloc(1, sizeof(til_stream_module_context_t) + n_contexts * sizeof(contexts[0]));
		if (!c)
			return -ENOMEM;

		c->path_hash = path_hash;
		c->n_module_contexts = n_contexts;
	}

	/* XXX: note in the reused case c->n_contexts may exceed n_contexts, but they're NULL */
	for (size_t i = 0; i < n_contexts; i++)
		c->module_contexts[i] = til_module_context_ref(contexts[i]);

	c->next = stream->module_context_buckets[bucket];
	stream->module_context_buckets[bucket] = c;

	return 0;
}


int til_stream_find_module_contexts(til_stream_t *stream, const char *path, size_t n_contexts, til_module_context_t **res_contexts)
{
	uint32_t			path_hash, bucket;
	til_stream_module_context_t	*c;

	assert(stream);
	assert(path);
	assert(n_contexts > 0);
	assert(res_contexts);

	/* TODO: add a til_path_t which pairs the hash and char* so we do't keep recomputing it */
	path_hash = til_jenkins((uint8_t *)path, strlen(path) + 1);
	bucket = path_hash % TIL_STREAM_CTXT_BUCKETS_COUNT;
	for (c = stream->module_context_buckets[bucket]; c != NULL; c = c->next) {
		if (c->path_hash != path_hash)
			continue;

		if (!strcmp(c->module_contexts[0]->setup->path, path)) {
			/* FIXME TODO: n_contexts mismatches need to be dealt with still:
			 *
			 * When n_contexts is smaller than the registered set, it seems trivial to
			 * just use the subset - but then the subset marches ahead of the others and
			 * should arguably invalidate the rest, truncating the set.  The complexity there
			 * is all the outstanding references need to realize their invalidated and refresh
			 * their contexts, presumably via this lookup in the stream.
			 *
			 * When n_contexts exceeds the registered set, there's a need to create more but
			 * they need to be identical, which suggests there needs to be
			 * til_module_t.clone_context()
			 *
			 * It should be noted that there's just one user of cloned contexts as a means of
			 * achieving parallel rendering: checkers::fill_module, if that usage is deprecated
			 * this all goes away.  But the way checkers is achieving parallelism there, regardless
			 * of what the underlying fill_module is capable of, is arguably a powerful thing, for
			 * use cases that need instancing of the same module's output.  Though maybe it makes
			 * more sense for checkers to be rendering _one_ tile, then copying it to the filled
			 * cells in parallel - if they're all expected to be identical, why not render it once?
			 */

			/* XXX: until the above is addressed, this assert will remain */
			assert(n_contexts <= c->n_module_contexts);
			for (size_t i = 0; i < n_contexts; i++)
				res_contexts[i] = til_module_context_ref(c->module_contexts[i]);

			return 0;
		}
	}

	return -ENOENT;
}


void til_stream_gc_module_contexts(til_stream_t *stream)
{
	unsigned	freed;

	assert(stream);

	/* This may not remain long-term, but there's no current way to unregister contexts - and by
	 * occasionally calling this at appropriate times, long-running modules potentially reusing
	 * contexts by path on streams like rkt can cleanup contexts rather than keep leaking them
	 * since they're automatically registered on-stream.
	 *
	 * But the way the "ref" builtin currently looks up contexts late @ render time presents some
	 * difficulty as to "when is the appropriate time to gc contexts?".  rtv for example is calling
	 * it immediately after creating the context for the next channel, it's just that rtv doesn't
	 * actually make use of context passing or anything like that via "ref" so it doesn't really
	 * matter - rtv just wants to release the resources.  For rkt, it gets more complicated, and
	 * it's unclear yet what the right solution will be there.  So consider this "gc" implementation
	 * very tentative and mostly just to keep "rtv" from meltdown while the dust settles on the rkt
	 * front.
	 */
	do {
		freed = 0;

		for (size_t b = 0; b < TIL_STREAM_CTXT_BUCKETS_COUNT; b++) {
			til_stream_module_context_t	*c, *c_prev, *c_next;

			for (c = stream->module_context_buckets[b], c_prev = NULL; c != NULL; c = c_next) {
				size_t	i;

				c_next = c->next;

				for (i = 0; i < c->n_module_contexts; i++) {
					if (c->module_contexts[i]->refcount != 1)
						break;
				}

				if (i < c->n_module_contexts) {
					c_prev = c;
					continue;
				}

				/* if all the contexts in the set are rc=1, that implies they're only on-stream
				 * and not actively referenced by any user, so we "gc" them.
				 */
				{ /* free(stream_module_context_t) */
					for (i = 0; i < c->n_module_contexts; i++)
						c->module_contexts[i] = til_module_context_free(c->module_contexts[i]);
					free(c);
					freed = 1;
				}

				if (c_prev)
					c_prev->next = c_next;
				else
					stream->module_context_buckets[b] = c_next;
			}
		}
	} while (freed);
}


static int til_stream_fprint_module_context_cb(void *arg, til_stream_module_context_t *module_context, size_t n_module_contexts, const til_module_context_t **contexts)
{
	FILE	*out = arg;

	fprintf(out, " %s: %s[%zu]", contexts[0]->setup->path, contexts[0]->module->name, n_module_contexts);
	for (size_t i = 0; i < n_module_contexts; i++) {
		fprintf(out,
			"%s{rc=%u, n_cpus=%u r=%u rd=%u maxrd=%u}",
			i ? ", " : " ",
			contexts[i]->refcount,
			contexts[i]->n_cpus,
			contexts[i]->renders_count,
			contexts[i]->last_render_duration,
			contexts[i]->max_render_duration);
	}
	fprintf(out, "\n");

	return 0;
}


void til_stream_fprint_module_contexts(til_stream_t *stream, FILE *out)
{
	/* TODO: errors */
	fprintf(out, "Module contexts on stream %p:\n", stream);
	(void) til_stream_for_each_module_context(stream, til_stream_fprint_module_context_cb, out);
	fprintf(out, "\n");
}
