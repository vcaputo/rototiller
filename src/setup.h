#ifndef _SETUP_H
#define _SETUP_H

#include "til_settings.h"

int setup_interactively(til_settings_t *settings, int (*setup_func)(til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc), int defaults);

#endif
