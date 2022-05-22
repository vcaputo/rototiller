#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "til.h"
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

static pixbounce_setup_t pixbounce_default_setup = {
        .pixmap_size = DEFAULT_PIXMAP_SIZE,
	.pixmap = DEFAULT_PIXMAP
};

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
	int			x, y;
	int			x_dir, y_dir;
	pixbounce_pixmap_t	*pix;
	uint32_t		color;
	float			pixmap_size_factor;
	int			multiplier;

} pixbounce_context_t;

static uint32_t pick_color()
{
	return makergb(rand()%256, rand()%256, rand()%256, 1);
}

static void * pixbounce_create_context(unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	pixbounce_context_t *ctxt;

	ctxt = malloc(sizeof(pixbounce_context_t));
	if (!ctxt)
		return NULL;

	ctxt->x = -1;
	ctxt->y = -1;
	ctxt->x_dir = 0;
	ctxt->y_dir = 0;
	ctxt->pix = &pixbounce_pixmap[((pixbounce_setup_t *)setup)->pixmap];
	ctxt->color = pick_color();
	ctxt->pixmap_size_factor = ((((pixbounce_setup_t *)setup)->pixmap_size)*55 + 22 )/ 100;
	ctxt->multiplier = 1;

	return ctxt;
}

static void pixbounce_destroy_context(void *context)
{
	pixbounce_context_t *ctxt = context;
	free(context);
}

static void pixbounce_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	pixbounce_context_t *ctxt = context;

	int	width = fragment->width, height = fragment->height;

	/* check for very small fragment */
	if(ctxt->pix->width*2>width||ctxt->pix->height*2>height)
		return;

	if(ctxt->x == -1) {
		int	multiplier_x, multiplier_y;

		/* calculate multiplyer for the pixmap */
		multiplier_x = width / ctxt->pix->width;
		multiplier_y = height / ctxt->pix->height;

		if(multiplier_x>=multiplier_y) {
			ctxt->multiplier = multiplier_y * (ctxt->pixmap_size_factor*55 + 22 )/ 100;
		} else {
			ctxt->multiplier = multiplier_x * (ctxt->pixmap_size_factor*55 + 22 ) / 100;
		}

		/* randomly initialize location and direction of pixmap */
		ctxt->x = rand() % (width - ctxt->pix->width * ctxt->multiplier) + 1;
		ctxt->y = rand() % (height - ctxt->pix->height * ctxt->multiplier) + 1;
		ctxt->x_dir = (rand() % 7) - 3;
		ctxt->y_dir = (rand() % 7) - 3;

	}

	/* blank the frame */
	til_fb_fragment_clear(fragment);

	/* translate pixmap to multiplier size and draw it to the fragment */
	for(int cursor_y=0; cursor_y < ctxt->pix->height*ctxt->multiplier; cursor_y++) {
		for(int cursor_x=0; cursor_x < ctxt->pix->width*ctxt->multiplier; cursor_x++) {
			int pix_offset = ((cursor_y/ctxt->multiplier)*ctxt->pix->width) + (cursor_x/ctxt->multiplier);
			if(ctxt->pix->pix_map[pix_offset] == 0) continue;
			til_fb_fragment_put_pixel_unchecked(
					fragment, TIL_FB_DRAW_FLAG_TEXTURABLE, ctxt->x+cursor_x, ctxt->y+cursor_y,
					ctxt->color
				);
		}
	}

	/* update pixmap location */
	if(ctxt->x+ctxt->x_dir < 0 || ctxt->x+ctxt->pix->width*ctxt->multiplier+ctxt->x_dir > width) {
		ctxt->x_dir = ctxt->x_dir * -1;
		ctxt->color = pick_color();
	}
	if(ctxt->y+ctxt->y_dir < 0 || ctxt->y+ctxt->pix->height*ctxt->multiplier+ctxt->y_dir > height) {
		ctxt->y_dir = ctxt->y_dir * -1;
		ctxt->color = pick_color();
	}
	ctxt->x = ctxt->x+ctxt->x_dir;
	ctxt->y = ctxt->y+ctxt->y_dir;
}

int pixbounce_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*pixmap_size;
	const char	*pixmap_size_values[] = {
				"0",
				"0.2",
				"0.4",
				"0.6",
				"0.8",
				"1",
				NULL
			};
	const char	*pixmap;
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

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
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

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_desc_t){
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

		setup = til_setup_new(sizeof(*setup), (void(*)(til_setup_t *))free);
		if (!setup)
			return -ENOMEM;

		sscanf(pixmap_size, "%f", &setup->pixmap_size);

		if (!strcasecmp(pixmap, "smiley")) {
			setup->pixmap = PIXBOUNCE_PIXMAP_SMILEY;
		} else if (!strcasecmp(pixmap, "crosshairs")) {
			setup->pixmap = PIXBOUNCE_PIXMAP_CROSSHAIRS;
		} else if (!strcasecmp(pixmap, "no")) {
			setup->pixmap = PIXBOUNCE_PIXMAP_NO;
		} else if (!strcasecmp(pixmap, "circles")) {
			setup->pixmap = PIXBOUNCE_PIXMAP_CIRCLES;
		} else if (!strcasecmp(pixmap, "qr_til")) {
			setup->pixmap = PIXBOUNCE_PIXMAP_QR_TIL;
		} else if (!strcasecmp(pixmap, "ignignokt")) {
			setup->pixmap = PIXBOUNCE_PIXMAP_IGNIGNOKT;
		} else if (!strcasecmp(pixmap, "err")) {
			setup->pixmap = PIXBOUNCE_PIXMAP_ERR;
		} else {
			til_setup_free(&setup->til_setup);

			return -EINVAL;
		}

		*res_setup = &setup->til_setup;
	}

	return 0;
}

til_module_t	pixbounce_module = {
	.create_context  = pixbounce_create_context,
	.destroy_context = pixbounce_destroy_context,
	.render_fragment = pixbounce_render_fragment,
	.setup = pixbounce_setup,
	.name = "pixbounce",
	.description = "Pixmap bounce",
	.author = "Philip J Freeman <elektron@halo.nu>",
	.flags = TIL_MODULE_OVERLAYABLE,
};
