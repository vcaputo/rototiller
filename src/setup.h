#ifndef _SETUP_H
#define _SETUP_H

#include "settings.h"

int setup_interactively(settings_t *settings, int (*setup_func)(settings_t *settings, setting_desc_t **next), int defaults);

#endif
