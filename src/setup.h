#ifndef _SETUP_H
#define _SETUP_H

#include "til_settings.h"
#include "til_setup.h"

int setup_interactively(til_settings_t *settings, int (*setup_func)(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup), int defaults, til_setup_t **res_setup, const char **res_failed_desc_path);

#endif
