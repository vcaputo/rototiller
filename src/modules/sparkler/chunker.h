#ifndef _CHUNKER_H
#define _CHUNKER_H

typedef struct chunker_t chunker_t;

chunker_t * chunker_new(unsigned chunk_size);
void * chunker_alloc(chunker_t *chunker, unsigned size);
void chunker_free(void *mem);
void chunker_free_chunker(chunker_t *chunker);

#endif
