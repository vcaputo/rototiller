#include <stddef.h>
#include <stdint.h>

#include "til_jenkins.h"

/* we just need something to hash paths/names, it's not super perf sensitive since
 * the hashes will be cached @ path/name intialization (they don't change).
 */

/* simple "one at a time" variant from https://en.wikipedia.org/wiki/Jenkins_hash_function */
uint32_t til_jenkins(const uint8_t *key, size_t length)
{
	uint32_t hash = 0;

	for (size_t i = 0; i < length; i++) {
		hash += key[i];
		hash += hash << 10;
		hash ^= hash >> 6;
	}

	hash += hash << 3;
	hash ^= hash >> 11;
	hash += hash << 15;

	return hash;
}
