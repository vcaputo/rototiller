#include <stdint.h>
#include <inttypes.h>
#include <math.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_util.h"

#include "ray/ray_camera.h"
#include "ray/ray_object.h"
#include "ray/ray_render.h"
#include "ray/ray_scene.h"

/* Copyright (C) 2016-2017 Vito Caputo <vcaputo@pengaru.com> */

static ray_object_t	objects[] = {
	{
		.plane = {
			.type = RAY_OBJECT_TYPE_PLANE,
			.surface = {
				.color = { .x = 0.6, .y = 0.3, .z = 0.8 },
				.diffuse = 1.0f,
				.specular = 0.2f,
				.highlight_exponent = 20.0f
			},
			.normal = { .x = 0.0, .y = 1.0, .z = 0.0 },
			.distance = 2.0f,
		}
	}, {
		.sphere = {
			.type = RAY_OBJECT_TYPE_SPHERE,
			.surface = {
				.color = { .x = 1.0, .y = 0.0, .z = 0.0 },
				.diffuse = 1.0f,
				.specular = 0.05f,
				.highlight_exponent = 20.0f
			},
			.center = { .x = 0.5, .y = 1.0, .z = 0.0 },
			.radius = 1.2f,
		}
	}, {
		.sphere = {
			.type = RAY_OBJECT_TYPE_SPHERE,
			.surface = {
				.color = { .x = 0.0, .y = 0.0, .z = 1.0 },
				.diffuse = 0.9f,
				.specular = 0.4f,
				.highlight_exponent = 20.0f
			},
			.center = { .x = -2.0, .y = 1.0, .z = 0.0 },
			.radius = 0.9f,
		}
	}, {
		.sphere = {
			.type = RAY_OBJECT_TYPE_SPHERE,
			.surface = {
				.color = { .x = 0.0, .y = 1.0, .z = 1.0 },
				.diffuse = 0.9f,
				.specular = 0.3f,
				.highlight_exponent = 20.0f
			},
			.center = { .x = 2.0, .y = -1.0, .z = 0.0 },
			.radius = 1.0f,
		}
	}, {
		.sphere = {
			.type = RAY_OBJECT_TYPE_SPHERE,
			.surface = {
				.color = { .x = 0.0, .y = 1.0, .z = 0.0 },
				.diffuse = 0.95f,
				.specular = 0.85f,
				.highlight_exponent = 1500.0f
			},
			.center = { .x = 0.2, .y = -1.25, .z = 0.0 },
			.radius = 0.6f,
		}
	}, {
		.type = RAY_OBJECT_TYPE_SENTINEL,
	}
};

static ray_object_t	lights[] = {
	{
		.light = {
			.type = RAY_OBJECT_TYPE_LIGHT,
			.brightness = 15.0f,
			.emitter = {
				.point.type = RAY_LIGHT_EMITTER_TYPE_POINT,
				.point.center = { .x = 3.0f, .y = 3.0f, .z = 3.0f },
				.point.surface = {
					.color = { .x = 1.0f, .y = 1.0f, .z = 1.0f },
				},
			}
		}
	}, {
		.type = RAY_OBJECT_TYPE_SENTINEL,
	}
};

static ray_camera_t	camera = {
	.position = { .x = 0.0, .y = 0.0, .z = 6.0 },
	.orientation = {
		.order = RAY_EULER_ORDER_YPR, /* yaw,pitch,roll */
		.yaw = RAY_EULER_DEGREES(0.0f),
		.pitch = RAY_EULER_DEGREES(0.0f),
		.roll = RAY_EULER_DEGREES(0.0f),
	},
	.focal_length = 700.0f,

	/* TODO: these should probably be adjusted @ runtime to at least fit the aspect ratio
	 * of the frame being rendered.
	 */
	.film_width = 1000.f,
	.film_height = 900.f,
};

static ray_scene_t	scene = {
	.objects = objects,
	.lights = lights,
	.ambient_color = { .x = 1.0f, .y = 1.0f, .z = 1.0f },
	.ambient_brightness = .1f,
	.gamma = .55f,
};

static float	r;


typedef struct ray_context_t {
	til_module_context_t	til_module_context;
	ray_render_t		*render;
} ray_context_t;


static til_module_context_t * ray_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, til_setup_t *setup)
{
	ray_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(ray_context_t), stream, seed, ticks, n_cpus, setup);
	if (!ctxt)
		return NULL;

	return &ctxt->til_module_context;
}


/* prepare a frame for concurrent rendering */
static void ray_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	ray_context_t		*ctxt = (ray_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_tile64 };
#if 1
	/* animated point light source */

	r += -.02;

	scene.lights[0].light.emitter.point.center.x = cosf(r) * 4.5f;
	scene.lights[0].light.emitter.point.center.z = sinf(r * 3.0f) * 4.5f;

	/* move the camera in a circle */
	camera.position.x = sinf(r) * (cosf(r) * 2.0f + 5.0f);
	camera.position.z = cosf(r) * (cosf(r) * 2.0f + 5.0f);

	/* also move up and down */
	camera.position.y = cosf(r * 1.3f) * 4.0f + 2.08f;

	/* keep camera facing the origin */
	camera.orientation.yaw = r + RAY_EULER_DEGREES(180.0f);


	/* tilt camera pitch in time with up and down movements, phase shifted appreciably */
	camera.orientation.pitch = -(sinf((M_PI * 1.5f) + r * 1.3f) * .6f + -.35f);
#endif
	ctxt->render = ray_render_new(&scene, &camera, fragment->frame_width, fragment->frame_height);
}


/* ray trace a simple scene into the fragment */
static void ray_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	ray_context_t		*ctxt = (ray_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	ray_render_trace_fragment(ctxt->render, fragment);
}


static void ray_finish_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr)
{
	ray_context_t	*ctxt = (ray_context_t *)context;

	ray_render_free(ctxt->render);
}


til_module_t	ray_module = {
	.create_context = ray_create_context,
	.prepare_frame = ray_prepare_frame,
	.render_fragment = ray_render_fragment,
	.finish_frame = ray_finish_frame,
	.name = "ray",
	.description = "Ray tracer (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
