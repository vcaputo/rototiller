#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "chunker.h"
#include "container.h"
#include "list.h"

/* Everything associated with the particles tends to be short-lived.
 *
 * They come and go frequently in large numbers.  This implements a very basic
 * chunked allocator which prioritizes efficient allocation and freeing over
 * low waste of memory.  We malloc chunks at a time, doling out elements from
 * the chunk sequentially as requested until the chunk is cannot fulfill an
 * allocation, then we just retire the chunk, add a new chunk and continue.
 *
 * When allocations are freed, we simply decrement the refcount for its chunk,
 * leaving the chunk pinned with holes accumulating until its refcount reaches
 * zero, at which point the chunk is made available for allocations again.
 *
 * This requires a reference to the chunk be returned with every allocation.
 * It may be possible to reduce the footprint of this by using a relative
 * offset to the chunk start instead, but that would probably be more harmful
 * to the alignment.
 *
 * This has some similarities to a slab allocator...
 */

#define CHUNK_ALIGNMENT			8192	/* XXX: this may be unnecessary, callers should be able to ideally size their chunkers */
#define ALLOC_ALIGNMENT			8	/* allocations within the chunk need to be aligned since their size affects subsequent allocation offsets */
#define ALIGN(_size, _alignment)	(((_size) + _alignment - 1) & ~(_alignment - 1))

typedef struct chunk_t {
	chunker_t	*chunker;	/* chunker chunk belongs to */
	list_head_t	chunks;		/* node on free/pinned list */
	uint32_t	n_refs;		/* number of references (allocations) to this chunk */
	unsigned	next_offset;	/* next available offset for allocation */
	uint8_t		mem[];		/* usable memory from this chunk */
} chunk_t;

typedef struct allocation_t {
	chunk_t		*chunk;		/* chunk this allocation came from */
	uint8_t		mem[];		/* usable memory from this allocation */
} allocation_t;

struct chunker_t {
	chunk_t		*chunk;		/* current chunk allocations come from */
	unsigned	chunk_size;	/* size chunks are allocated in */
	list_head_t	free_chunks;	/* list of completely free chunks */
	list_head_t	pinned_chunks;	/* list of chunks pinned because they have an outstanding allocation */
};


/* Add a reference to a chunk. */
static inline void chunk_ref(chunk_t *chunk)
{
	assert(chunk);
	assert(chunk->chunker);

	chunk->n_refs++;

	assert(chunk->n_refs != 0);
}


/* Remove reference from a chunk, move to free list when no references remain. */
static inline void chunk_unref(chunk_t *chunk)
{
	assert(chunk);
	assert(chunk->chunker);
	assert(chunk->n_refs > 0);

	chunk->n_refs--;
	if (chunk->n_refs == 0) {
		list_move(&chunk->chunks, &chunk->chunker->free_chunks);
	}
}


/* Return allocated size of the chunk */
static inline unsigned chunk_alloc_size(chunker_t *chunker)
{
	assert(chunker);

	return (sizeof(chunk_t) + chunker->chunk_size);
}


/* Get a new working chunk, retiring and replacing chunker->chunk. */
static void chunker_new_chunk(chunker_t *chunker)
{
	chunk_t	*chunk;

	assert(chunker);

	if (chunker->chunk) {
		chunk_unref(chunker->chunk);
		chunker->chunk = NULL;
	}

	if (!list_empty(&chunker->free_chunks)) {
		chunk = list_entry(chunker->free_chunks.next, chunk_t, chunks);
		list_del(&chunk->chunks);
	} else {
		/* No free chunks, must ask libc for memory */
		chunk = malloc(chunk_alloc_size(chunker));
	}

	/* Note a chunk is pinned from the moment it's created, and a reference
	 * is added to represent chunker->chunk, even though no allocations
	 * occurred yet.
	 */
	chunk->n_refs = 1;
	chunk->next_offset = 0;
	chunk->chunker = chunker;
	chunker->chunk = chunk;
	list_add(&chunk->chunks, &chunker->pinned_chunks);
}


/* Create a new chunker. */
chunker_t * chunker_new(unsigned chunk_size)
{
	chunker_t	*chunker;

	chunker = calloc(1, sizeof(chunker_t));
	if (!chunker) {
		return NULL;
	}

	INIT_LIST_HEAD(&chunker->free_chunks);
	INIT_LIST_HEAD(&chunker->pinned_chunks);

	/* XXX: chunker->chunk_size does not include the size of the chunk_t container */
	chunker->chunk_size = ALIGN(chunk_size, CHUNK_ALIGNMENT);

	return chunker;
}


/* Allocate non-zeroed memory from a chunker. */
void * chunker_alloc(chunker_t *chunker, unsigned size)
{
	allocation_t	*allocation;

	assert(chunker);
	assert(size <= chunker->chunk_size);

	size = ALIGN(sizeof(allocation_t) + size, ALLOC_ALIGNMENT);

	if (!chunker->chunk || size + chunker->chunk->next_offset > chunker->chunk_size) {
		/* Retire this chunk, time for a new one */
		chunker_new_chunk(chunker);
	}

	if (!chunker->chunk) {
		return NULL;
	}

	chunk_ref(chunker->chunk);
	allocation = (allocation_t *)&chunker->chunk->mem[chunker->chunk->next_offset];
	chunker->chunk->next_offset += size;
	allocation->chunk = chunker->chunk;

	assert(chunker->chunk->next_offset <= chunker->chunk_size);

	return allocation->mem;
}


/* Free memory allocated from a chunker. */
void chunker_free(void *ptr)
{
	allocation_t	*allocation = container_of(ptr, allocation_t, mem);

	assert(ptr);

	chunk_unref(allocation->chunk);
}


/* Free a chunker and it's associated allocations. */
void chunker_free_chunker(chunker_t *chunker)
{
	chunk_t	*chunk, *_chunk;

	assert(chunker);

	if (chunker->chunk) {
		chunk_unref(chunker->chunk);
	}

/*
	It can be useful to police this, but part of the value of the chunker
	is to be able to perform a bulk cleanup without first performing heaps
	of granular frees.  Maybe enforcing this should be requestable via a
	parameter.
	assert(list_empty(&chunker->pinned_chunks));
*/

	list_for_each_entry_safe(chunk, _chunk, &chunker->free_chunks, chunks) {
		free(chunk);
	}

	free(chunker);
}

/* TODO: add pinned chunk iterator interface for cache-friendly iterating across
 * chunk contents.
 * The idea is that at times when the performance is really important, the
 * chunks will be full of active particles, because it's the large numbers
 * which slows us down.  At those times, it's beneficial to not walk linked
 * lists of structs to process them, instead we just process all the elements
 * of the chunk as an array and assume everything is active.  The type of
 * processing being done in this fashion is benign to perform on an unused
 * element, as long as there's no dangling pointers being dereferenced.  If
 * there's references, a status field could be maintained in the entry to say
 * if it's active, then simply skip processing of the inactive elements.  This
 * tends to be more cache-friendly than chasing pointers.  A linked list
 * heirarchy of particles is still maintained for the parent:child
 * relationships under the assumption that some particles will make use of the
 * tracked descendants, though nothing has been done with it yet.
 *
 * The current implementation of the _particle_t is variable length, which precludes
 * this optimization.  However, breaking out the particle_props_t into a separate
 * chunker would allow running particles_age() across the props alone directly
 * within the pinned chunks.  The other passes are still done heirarchically,
 * and require the full particle context.
 */
