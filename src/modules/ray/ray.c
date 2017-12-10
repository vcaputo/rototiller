#include <stdint.h>
#include <inttypes.h>
#include <math.h>

#include "fb.h"
#include "rototiller.h"
#include "util.h"

#include "ray_camera.h"
#include "ray_object.h"
#include "ray_render.h"
#include "ray_scene.h"

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
			.brightness = 1.0,
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
};

static ray_scene_t	scene = {
	.objects = objects,
	.lights = lights,
	.ambient_color = { .x = 1.0f, .y = 1.0f, .z = 1.0f },
	.ambient_brightness = .04f,
};

static float	r;


typedef struct ray_context_t {
	ray_render_t	*render;
} ray_context_t;


static void * ray_create_context(void)
{
	return calloc(1, sizeof(ray_context_t));
}


static void ray_destroy_context(void *context)
{
	free(context);
}


static int ray_fragmenter(void *context, const fb_fragment_t *fragment, unsigned num, fb_fragment_t *res_fragment)
{
	return fb_fragment_tile_single(fragment, 64, num, res_fragment);
}


/* prepare a frame for concurrent rendering */
static void ray_prepare_frame(void *context, unsigned n_cpus, fb_fragment_t *fragment, rototiller_fragmenter_t *res_fragmenter)
{
	ray_context_t	*ctxt = context;

	*res_fragmenter = ray_fragmenter;

	/* TODO: the camera doesn't need the width and height anymore, the fragment has the frame_width/frame_height */
	camera.width = fragment->frame_width,
	camera.height = fragment->frame_height,
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
	ctxt->render = ray_render_new(&scene, &camera);
}


/* ray trace a simple scene into the fragment */
static void ray_render_fragment(void *context, fb_fragment_t *fragment)
{
	ray_context_t	*ctxt = context;

	ray_render_trace_fragment(ctxt->render, fragment);
}


static void ray_finish_frame(void *context, fb_fragment_t *fragment)
{
	ray_context_t	*ctxt = context;

	ray_render_free(ctxt->render);
}


rototiller_module_t	ray_module = {
	.create_context = ray_create_context,
	.destroy_context = ray_destroy_context,
	.prepare_frame = ray_prepare_frame,
	.render_fragment = ray_render_fragment,
	.finish_frame = ray_finish_frame,
	.name = "ray",
	.description = "Ray tracer (threaded)",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
