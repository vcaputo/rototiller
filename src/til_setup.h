#ifndef _TIL_SETUP_H
#define _TIL_SETUP_H

typedef struct til_setup_t til_setup_t;

struct til_setup_t {
	void (*free)(til_setup_t *setup);
};

#endif
