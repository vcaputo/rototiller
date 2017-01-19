#ifndef _CONTAINER_H
#define _CONTAINER_H

#include <stddef.h>

#ifndef container_of
#define container_of(_ptr, _type, _member) \
	(_type *)((void *)(_ptr) - offsetof(_type, _member))
#endif

#endif
