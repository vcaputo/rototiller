#include <assert.h>
#include <stdlib.h>

#include "til_fb.h"
#include "til_settings.h"
#include "til_util.h"
#include "til_video_setup.h"

/* dummy mem_fb backend; render to anonymous memory */
/* useful for testing/debugging, and benchmarking systems even if headless */

typedef struct mem_fb_page_t mem_fb_page_t;

struct mem_fb_page_t {
	mem_fb_page_t		*next_spare;
	uint32_t		buf[];
};

typedef struct mem_fb_setup_t {
	til_video_setup_t	til_video_setup;
	unsigned		width, height;
} mem_fb_setup_t;

typedef struct mem_fb_t {
	mem_fb_setup_t		setup;
	mem_fb_page_t		*spare_pages;
} mem_fb_t;


static int mem_fb_init(const char *title, const til_video_setup_t *setup, void **res_context)
{
	mem_fb_t	*c;
	int		r;

	assert(setup);
	assert(res_context);

	c = calloc(1, sizeof(mem_fb_t));
	if (!c) {
		r = -ENOMEM;
		goto _err;
	}

	c->setup = *(mem_fb_setup_t *)setup;

	*res_context = c;

	return 0;

_err:
	return r;
}


static void mem_fb_shutdown(til_fb_t *fb, void *context)
{
	mem_fb_t	*c = context;
	mem_fb_page_t	*p;

	assert(c);

	while ((p = c->spare_pages)) {
		c->spare_pages = p->next_spare;
		free(p);
	}

	free(c);
}


static int mem_fb_acquire(til_fb_t *fb, void *context, void *page)
{
	return 0;
}


static void mem_fb_release(til_fb_t *fb, void *context)
{
}


static void * mem_fb_page_alloc(til_fb_t *fb, void *context, til_fb_fragment_t *res_fragment)
{
	mem_fb_t	*c = context;
	mem_fb_page_t	*p = NULL;

	if (c->spare_pages) {
		p = c->spare_pages;
		c->spare_pages = p->next_spare;
	}

	if (!p) {
		p = calloc(1, sizeof(mem_fb_page_t) + c->setup.width * c->setup.height * sizeof(uint32_t));
		if (!p)
			return NULL;
	}

	*res_fragment =	(til_fb_fragment_t){
				.buf = p->buf,
				.width = c->setup.width,
				.frame_width = c->setup.width,
				.height = c->setup.height,
				.frame_height = c->setup.height,
				.pitch = c->setup.width,
			};

	return p;
}


static int mem_fb_page_free(til_fb_t *fb, void *context, void *page)
{
	mem_fb_t	*c = context;
	mem_fb_page_t	*p = page;

	p->next_spare = c->spare_pages;
	c->spare_pages = p;

	return 0;
}


static int mem_fb_page_flip(til_fb_t *fb, void *context, void *page)
{
	/* TODO: add a timer for supporting an fps setting? */
	return 0;
}


static int mem_fb_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup);


til_fb_ops_t mem_fb_ops = {
	.setup = mem_fb_setup,
	.init = mem_fb_init,
	.shutdown = mem_fb_shutdown,
	.acquire = mem_fb_acquire,
	.release = mem_fb_release,
	.page_alloc = mem_fb_page_alloc,
	.page_free = mem_fb_page_free,
	.page_flip = mem_fb_page_flip
};


static int mem_fb_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	til_setting_t	*size;
	int		r;

	r = til_settings_get_and_describe_setting(settings,
						&(til_setting_spec_t){
							.name = "Virtual window size",
							.key = "size",
							.regex = "[1-9][0-9]*[xX][1-9][0-9]*",
							.preferred = "640x480",
							.values = NULL,
							.annotations = NULL
						},
						&size,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (res_setup) {
		mem_fb_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL, &mem_fb_ops);
		if (!setup)
			return -ENOMEM;

		if (sscanf(size->value, "%ux%u", &setup->width, &setup->height) != 2)
			return til_setup_free_with_failed_setting_ret_err(&setup->til_video_setup.til_setup, size, res_setting, -EINVAL);

		*res_setup = &setup->til_video_setup.til_setup;
	}

	return 0;
}


