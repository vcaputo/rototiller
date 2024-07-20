/*
 *  Copyright (C) 2019 - Vito Caputo - <vcaputo@pengaru.com>
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

#ifndef _TXT_H
#define _TXT_H

#include <stdint.h>

typedef struct til_fb_fragment_t til_fb_fragment_t;
typedef struct txt_t txt_t;

typedef enum txt_halign_t {
	TXT_HALIGN_CENTER,
	TXT_HALIGN_LEFT,
	TXT_HALIGN_RIGHT,
	TXT_HALIGN_CNT,
} txt_halign_t;

typedef enum txt_valign_t {
	TXT_VALIGN_CENTER,
	TXT_VALIGN_TOP,
	TXT_VALIGN_BOTTOM,
	TXT_VALIGN_CNT,
} txt_valign_t;

typedef struct txt_align_t {
	txt_halign_t	horiz;
	txt_valign_t	vert;
} txt_align_t;

txt_t * txt_new(const char *str);
txt_t * txt_newf(const char *fmt, ...);
txt_t * txt_free(txt_t *txt);
void txt_render_fragment_aligned(txt_t *txt, til_fb_fragment_t *fragment, uint32_t color, int x, int y, txt_align_t alignment);
void txt_render_fragment_offsetted(txt_t *txt, til_fb_fragment_t *fragment, uint32_t color, int x, int y, float x_offset, float y_offset);

#endif
