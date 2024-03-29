#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "til.h"
#include "til_module_context.h"
#include "draw.h"

/* Copyright (C) 2018-22 Philip J. Freeman <elektron@halo.nu> */

#define DEFAULT_PIXMAP_SIZE  0.6
#define DEFAULT_PIXMAP PIXBOUNCE_PIXMAP_SMILEY

typedef enum pixbounce_pixmaps_t {
	PIXBOUNCE_PIXMAP_SMILEY,
	PIXBOUNCE_PIXMAP_CROSSHAIRS,
	PIXBOUNCE_PIXMAP_NO,
	PIXBOUNCE_PIXMAP_CIRCLES,
	PIXBOUNCE_PIXMAP_QR_TIL,
	PIXBOUNCE_PIXMAP_IGNIGNOKT,
	PIXBOUNCE_PIXMAP_ERR
} pixbounce_pixmaps_t;

typedef struct pixbounce_setup_t {
	til_setup_t		til_setup;
	float			pixmap_size;
	pixbounce_pixmaps_t	pixmap;
} pixbounce_setup_t;

typedef struct pixbounce_pixmap_t {
	int width, height;
	int pix_map[34*35];
} pixbounce_pixmap_t;

int num_pix = 7;

pixbounce_pixmap_t pixbounce_pixmap[] = {
	{ 16, 16,
		{
		0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
		0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
		0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
		0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
		0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0,
		1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1,
		1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
		1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
		1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
		0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0,
		0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0,
		0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0,
		0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
		0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
		},
	},
	{ 16, 16,
		{
		0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
		1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1,
		0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
		},
	},
	{ 16, 16,
		{
		0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
		0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
		0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
		0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
		1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
		1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1,
		1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1,
		1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1,
		1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1,
		1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
		0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
		0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
		0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
		0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
		},
	},
	{ 16, 16,
		{
		1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
		1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1,
		0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0,
		0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0,
		0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0,
		0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0,
		0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0,
		0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0,
		0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0,
		0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0,
		0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0,
		0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0,
		0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0,
		0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0,
		1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1,
		1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
		},
	},
	{ 25, 25,
		{
		1,1,1,1,1,1,1,0,0,0,1,1,0,1,0,1,0,0,1,1,1,1,1,1,1,
		1,0,0,0,0,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,0,0,1,
		1,0,1,1,1,0,1,0,1,0,1,1,0,0,1,1,0,0,1,0,1,1,1,0,1,
		1,0,1,1,1,0,1,0,1,1,1,0,0,0,1,0,1,0,1,0,1,1,1,0,1,
		1,0,1,1,1,0,1,0,0,0,0,1,0,1,0,1,0,0,1,0,1,1,1,0,1,
		1,0,0,0,0,0,1,0,0,1,0,0,0,1,1,1,0,0,1,0,0,0,0,0,1,
		1,1,1,1,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,1,1,1,1,
		0,0,0,0,0,0,0,0,0,1,0,0,1,0,1,1,1,0,0,0,0,0,0,0,0,
		0,0,0,1,1,0,1,1,0,0,0,0,0,1,0,1,1,0,0,0,0,1,1,0,0,
		1,0,0,0,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,1,1,1,1,1,0,
		1,1,1,0,1,1,1,0,1,0,1,1,1,0,0,1,1,1,1,1,1,1,0,0,1,
		1,0,0,1,0,1,0,0,1,0,0,0,1,1,1,1,1,1,0,1,0,1,1,0,0,
		1,0,0,0,1,0,1,1,0,1,1,1,1,1,1,0,0,1,1,1,0,1,0,0,0,
		1,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,1,1,1,0,0,
		1,1,0,0,1,0,1,0,1,0,0,1,1,0,1,1,0,1,0,1,0,0,0,1,1,
		1,0,1,1,0,0,0,1,1,0,1,0,1,1,1,0,1,0,0,1,0,1,1,0,0,
		1,0,1,1,0,1,1,1,1,0,0,1,0,0,0,1,1,1,1,1,1,0,1,1,1,
		0,0,0,0,0,0,0,0,1,1,1,0,1,0,1,0,1,0,0,0,1,1,1,0,0,
		1,1,1,1,1,1,1,0,1,0,0,1,1,0,0,1,1,0,1,0,1,1,1,0,1,
		1,0,0,0,0,0,1,0,0,1,0,1,1,1,0,1,1,0,0,0,1,1,0,0,0,
		1,0,1,1,1,0,1,0,1,1,1,0,1,1,0,0,1,1,1,1,1,1,0,0,0,
		1,0,1,1,1,0,1,0,1,1,0,1,0,0,1,0,0,1,0,0,0,0,0,0,1,
		1,0,1,1,1,0,1,0,0,1,1,1,0,1,1,0,1,1,1,1,1,0,0,1,1,
		1,0,0,0,0,0,1,0,0,1,1,1,0,0,0,1,1,1,1,0,0,1,1,1,1,
		1,1,1,1,1,1,1,0,0,0,1,1,1,0,0,1,1,0,1,0,0,1,0,0,1
		},
	},
	{ 34, 30,
		{
		0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,1,1,1,1,1,1,1,0,1,1,1,1,1,1,0,0,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,0,0,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,1,0,0,1,1,0,0,0,1,1,1,1,0,0,0,1,1,0,0,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
		0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
		0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,0,0,
		0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1,0,0,
		0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,
		1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,
		1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,
		1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,
		1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,
		0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
		},
	},
	{ 34, 35,
		{
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,0,0,0,1,1,1,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,1,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
		0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
		0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,
		0,0,1,1,0,1,1,1,1,1,0,0,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,0,0,1,1,0,0,
		1,1,1,1,0,1,1,1,1,1,0,0,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,0,0,1,1,1,1,
		1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,
		1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,
		1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,
		1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,
		1,1,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,1,1,
		0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
		0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0
		},
	},
};


typedef struct pixbounce_context_t {
	til_module_context_t	til_module_context;
	int			x, y;
	int			x_dir, y_dir;
	pixbounce_pixmap_t	*pix;
	uint32_t		color;
	float			pixmap_size_factor;
	float			multiplier;
} pixbounce_context_t;

static uint32_t pick_color(unsigned *seedp)
{
	return makergb(rand_r(seedp)%256, rand_r(seedp)%256, rand_r(seedp)%256, 1);
}

static til_module_context_t * pixbounce_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	pixbounce_context_t *ctxt;

	ctxt = til_module_context_new(module, sizeof(pixbounce_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	ctxt->x = -1;
	ctxt->y = -1;
	ctxt->x_dir = 0;
	ctxt->y_dir = 0;
	ctxt->pix = &pixbounce_pixmap[((pixbounce_setup_t *)setup)->pixmap];
	ctxt->color = pick_color(&ctxt->til_module_context.seed);
	ctxt->pixmap_size_factor = ((((pixbounce_setup_t *)setup)->pixmap_size)*55 + 22 )/ 100;
	ctxt->multiplier = 1;

	return &ctxt->til_module_context;
}

static void pixbounce_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	pixbounce_context_t	*ctxt = (pixbounce_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;
	int			width = fragment->frame_width, height = fragment->frame_height;

	/* tell rototiller how to subfragment the frame for threaded rendering */
	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_tile64 };

	if(ctxt->x == -1) {
		float	multiplier_x, multiplier_y;

		/* calculate multiplyer for the pixmap */
		multiplier_x = width / ctxt->pix->width;
		multiplier_y = height / ctxt->pix->height;

		if(multiplier_x>=multiplier_y) {
			ctxt->multiplier = multiplier_y * (ctxt->pixmap_size_factor * 55 + 22 ) * .01f;
		} else {
			ctxt->multiplier = multiplier_x * (ctxt->pixmap_size_factor * 55 + 22 ) * .01f;
		}

		/* randomly initialize location and direction of pixmap */
		ctxt->x = rand_r(&ctxt->til_module_context.seed) % (int)(width - ctxt->pix->width * ctxt->multiplier) + 1;
		ctxt->y = rand_r(&ctxt->til_module_context.seed) % (int)(height - ctxt->pix->height * ctxt->multiplier) + 1;
		ctxt->x_dir = (rand_r(&ctxt->til_module_context.seed) % 7) - 3;
		ctxt->y_dir = (rand_r(&ctxt->til_module_context.seed) % 7) - 3;

	}

	/* update pixmap location */
	if (ticks != context->last_ticks) {
		if(ctxt->x+ctxt->x_dir < 0 || ctxt->x+ctxt->pix->width*ctxt->multiplier+ctxt->x_dir > width) {
			ctxt->x_dir = ctxt->x_dir * -1;
			ctxt->color = pick_color(&ctxt->til_module_context.seed);
		}
		if(ctxt->y+ctxt->y_dir < 0 || ctxt->y+ctxt->pix->height*ctxt->multiplier+ctxt->y_dir > height) {
			ctxt->y_dir = ctxt->y_dir * -1;
			ctxt->color = pick_color(&ctxt->til_module_context.seed);
		}
		ctxt->x = ctxt->x+ctxt->x_dir;
		ctxt->y = ctxt->y+ctxt->y_dir;
	}
}

static void pixbounce_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	pixbounce_context_t	*ctxt = (pixbounce_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;
	int			pix_y, pix_x, pix_w, pix_h;
	float			inv_multiplier = 1.f / ctxt->multiplier;

	/* blank the fragment */
	til_fb_fragment_clear(fragment);

	/* check if this fragment is entirely outside the scaled pix */
	if (fragment->x > ctxt->x + ctxt->pix->width * ctxt->multiplier ||
	    fragment->x + fragment->width < ctxt->x ||
	    fragment->y > ctxt->y + ctxt->pix->height * ctxt->multiplier ||
	    fragment->y + fragment->height < ctxt->y)
		return;

	/* clip the scaled+placed pix to fragment->{x,y,width,height} */
	pix_x = MAX(ctxt->x, fragment->x) - ctxt->x;
	pix_y = MAX(ctxt->y, fragment->y) - ctxt->y;
	pix_w = MIN(ctxt->x + ctxt->pix->width * ctxt->multiplier, fragment->x + fragment->width) - (ctxt->x + pix_x) ;
	pix_h = MIN(ctxt->y + ctxt->pix->height * ctxt->multiplier, fragment->y + fragment->height) - (ctxt->y + pix_y);

	/* translate pixmap to multiplier size and draw it to the fragment */
	for(int cursor_y = 0; cursor_y < pix_h; cursor_y++) {
		for(int cursor_x = 0; cursor_x < pix_w; cursor_x++) {
			int pix_offset = ((int)((cursor_y+pix_y)*inv_multiplier)*ctxt->pix->width) + ((int)(cursor_x+pix_x)*inv_multiplier);
			if(ctxt->pix->pix_map[pix_offset] == 0) continue;
			til_fb_fragment_put_pixel_unchecked(
					fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, ctxt->x+pix_x+cursor_x, ctxt->y+pix_y+cursor_y,
					ctxt->color
				);
		}
	}
}

int pixbounce_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);

til_module_t	pixbounce_module = {
	.create_context  = pixbounce_create_context,
	.prepare_frame = pixbounce_prepare_frame,
	.render_fragment = pixbounce_render_fragment,
	.setup = pixbounce_setup,
	.name = "pixbounce",
	.description = "Pixmap bounce (threaded)",
	.author = "Philip J Freeman <elektron@halo.nu>",
	.flags = TIL_MODULE_OVERLAYABLE,
};

int pixbounce_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*pixmap_size;
	const char	*pixmap_size_values[] = {
				"0",
				"0.2",
				"0.4",
				"0.6",
				"0.8",
				"1",
				NULL
			};
	til_setting_t	*pixmap;
	const char	*pixmap_values[] = {
				"smiley",
				"crosshairs",
				"no",
				"circles",
				"qr_til",
				"ignignokt",
				"err",
				NULL
			};

	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Pixmap size",
							.key = "pixmap_size",
							.regex = "(0|1|0\\.[0-9]{1,2})",
							.preferred = TIL_SETTINGS_STR(DEFAULT_PIXMAP_SIZE),
							.values = pixmap_size_values,
							.annotations = NULL
						},
						&pixmap_size,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Pixmap",
							.key = "pixmap",
							.regex = ":[alnum]:+",
							.preferred = pixmap_values[DEFAULT_PIXMAP],
							.values = pixmap_values,
							.annotations = NULL
						},
						&pixmap,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		pixbounce_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &pixbounce_module);
		if (!setup)
			return -ENOMEM;

		if (sscanf(pixmap_size->value, "%f", &setup->pixmap_size) != 1)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, pixmap_size, res_setting, -EINVAL);

		if (!strcasecmp(pixmap->value, "smiley"))
			setup->pixmap = PIXBOUNCE_PIXMAP_SMILEY;
		else if (!strcasecmp(pixmap->value, "crosshairs"))
		      setup->pixmap = PIXBOUNCE_PIXMAP_CROSSHAIRS;
		else if (!strcasecmp(pixmap->value, "no"))
		      setup->pixmap = PIXBOUNCE_PIXMAP_NO;
		else if (!strcasecmp(pixmap->value, "circles"))
		      setup->pixmap = PIXBOUNCE_PIXMAP_CIRCLES;
		else if (!strcasecmp(pixmap->value, "qr_til"))
		      setup->pixmap = PIXBOUNCE_PIXMAP_QR_TIL;
		else if (!strcasecmp(pixmap->value, "ignignokt"))
		      setup->pixmap = PIXBOUNCE_PIXMAP_IGNIGNOKT;
		else if (!strcasecmp(pixmap->value, "err"))
		      setup->pixmap = PIXBOUNCE_PIXMAP_ERR;
		else
			return til_setup_free_with_failed_setting_ret_err(&setup->til_setup, pixmap, res_setting, -EINVAL);

		*res_setup = &setup->til_setup;
	}

	return 0;
}
