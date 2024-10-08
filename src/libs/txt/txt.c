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

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

#include "til_fb.h"

#include "ascii/ascii.h"
#include "txt.h"


struct txt_t {
	int	len, width, height;
	char	str[];
};


/* compute the rectangle dimensions of the string in rendered pixels */
static void measure_str(const char *str, int *res_width, int *res_height)
{
	int	rows = 1, cols = 0, col = 0;
	char	c;

	assert(str);
	assert(res_width);
	assert(res_height);

	while ((c = *str)) {
		switch (c) {
		case ' '...'~':
			col++;
			break;

		case '\n':
			if (col > cols)
				cols = col;
			col = 0;
			rows++;
			break;

		default:
			break;
		}
		str++;
	}

	if (col > cols)
		cols = col;

	*res_height = 1 + rows * (ASCII_HEIGHT + 1);
	*res_width = 1 + cols * (ASCII_WIDTH + 1);
}


txt_t * txt_new(const char *str)
{
	txt_t	*txt;
	int	len;

	assert(str);

	len = strlen(str);

	txt = calloc(1, sizeof(txt_t) + len + 1);
	if (!txt)
		return NULL;

	txt->len = len;
	memcpy(txt->str, str, len);

	measure_str(txt->str, &txt->width, &txt->height);

	return txt;
}


txt_t * txt_newf(const char *fmt, ...)
{
	char	buf[1024] = {};	/* XXX: it's not expected there will be long strings */
	va_list	ap;

	assert(fmt);

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);

	return txt_new(buf);
}


txt_t * txt_free(txt_t *txt)
{
	free(txt);

	return NULL;
}


/* Adjusts x and y according to alignment, width, and height.  Returning the adjusted x and y
 * in res_x, res_y.
 *
 * res_x,res_y will be the coordinate of the upper left corner of the rect
 * described by width,height when aligned relative to x,y according to the
 * specified alignment.
 *
 * e.g. if an alignment of TXT_HALIGN_LEFT,TXT_VALIGN_TOP is supplied, x,y is returned verbatim
 * in res_x,res_y.
 * an alignment of TXT_HALIGN_CENTER,TXT_VALIGN_CENTER returns x-width/2 and y-width/2.
 */
static void justify(txt_align_t alignment, int x, int y, unsigned width, unsigned height, int *res_x, int *res_y)
{
	assert(res_x);
	assert(res_y);

	switch (alignment.horiz) {
	case TXT_HALIGN_CENTER:
		x -= width >> 1;
		break;

	case TXT_HALIGN_LEFT:
		break;

	case TXT_HALIGN_RIGHT:
		x -= width;
		break;

	default:
		assert(0);
	}

	switch (alignment.vert) {
	case TXT_VALIGN_CENTER:
		y -= height >> 1;
		break;

	case TXT_VALIGN_TOP:
		break;

	case TXT_VALIGN_BOTTOM:
		y -= height;
		break;

	default:
		assert(0);
	}

	*res_x = x;
	*res_y = y;
}


static int overlaps(int x1, int y1, unsigned w1, unsigned h1, int x2, int y2, unsigned w2, unsigned h2)
{
	/* TODO */
	return 1;
}


static inline void draw_char(til_fb_fragment_t *fragment, uint32_t color, int x, int y, unsigned char c)
{
	/* TODO: this could be optimized to skip characters with no overlap */
	for (int i = 0; i < ASCII_HEIGHT; i++) {
		for (int j = 0; j < ASCII_WIDTH; j++) {
			if (ascii_chars[c][i * ASCII_WIDTH + j])
				til_fb_fragment_put_pixel_checked(fragment, 0, x + j, y + i, color);
		}
	}
}


static void txt_render(txt_t *txt, til_fb_fragment_t *fragment, uint32_t color, int jx, int jy)
{
	int	col, row;
	char	*str;

	assert(txt);
	assert(fragment);

	if (!overlaps(jx, jy, txt->width, txt->height,
		      fragment->x, fragment->y,
		      fragment->width, fragment->height))
		return;

	for (col = 0, row = 0, str = txt->str; *str; str++) {
		switch (*str) {
		case ' '...'~':
			draw_char(fragment, color, jx + 1 + col * (ASCII_WIDTH + 1), jy + 1 + row * (ASCII_HEIGHT + 1), *str);
			col++;
			break;

		case '\n':
			col = 0;
			row++;
			break;

		default:
			break;
		}
	}
}


void txt_render_fragment_aligned(txt_t *txt, til_fb_fragment_t *fragment, uint32_t color, int x, int y, txt_align_t alignment)
{
	int	jx, jy;

	assert(txt);

	justify(alignment, x, y, txt->width, txt->height, &jx, &jy);

	return txt_render(txt, fragment, color, jx, jy);
}


/* Like txt_render_fragment_aligned(), except instead of specifying halign/valign enum variants,
 * you provide -1..+1 normalized offsets within the rendered text rectangle to anchor the
 * provided x,y pixel coordinates.  This enables progressively varying the justification
 * offset rather than it behaving like a step function of three options left/right/center or
 * top/bottom/center etc.  (modules/asc was the impetus for adding this)
 */
void txt_render_fragment_offsetted(txt_t *txt, til_fb_fragment_t *fragment, uint32_t color, int x, int y, float x_offset, float y_offset)
{
	int	jx = x, jy = y;

	assert(txt);

	/* XXX: one could argue the offsets should be clamped to -1..+1, but
	 * I can see valid use cases where one wants to go grater than abs(1)
	 * to achieve extremely offset texts in dynamic situations.  So I'm
	 * just letting whatever comes in pass thru for now, considering this
	 * is "art" oriented where allowing creative stuff to just work trumps
	 * preventing potential buggy behavior.
	 */
	jx -= x_offset * txt->width * .5f + txt->width * .5f;
	jy -= y_offset * txt->height * .5f + txt->height * .5f;

	return txt_render(txt, fragment, color, jx, jy);
}
