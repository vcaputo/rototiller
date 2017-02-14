#include <stdint.h>
#include <inttypes.h>
#include <math.h>

#include "fb.h"
#include "rototiller.h"
#include "util.h"

#include "ray_camera.h"
#include "ray_object.h"
#include "ray_scene.h"
#include "ray_threads.h"

/* Copyright (C) 2016-2017 Vito Caputo <vcaputo@pengaru.com> */

/* ray trace a simple scene into the fragment */
static void ray(fb_fragment_t *fragment)
{
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
		}
	};

	ray_camera_t		camera = {
					.position = { .x = 0.0, .y = 0.0, .z = 6.0 },
					.orientation = {
						.order = RAY_EULER_ORDER_YPR, /* yaw,pitch,roll */
						.yaw = RAY_EULER_DEGREES(0.0f),
						.pitch = RAY_EULER_DEGREES(0.0f),
						.roll = RAY_EULER_DEGREES(180.0f),
					},
					.focal_length = 700.0f,
					.width = fragment->width,
					.height = fragment->height,
				};

	static ray_scene_t	scene = {
					.objects = objects,
					.n_objects = nelems(objects),
					.lights = &objects[5],
					.n_lights = 1,
					.ambient_color = { .x = 1.0f, .y = 1.0f, .z = 1.0f },
					.ambient_brightness = .04f,
				};
	static int		initialized;
	static ray_threads_t	*threads;
	static fb_fragment_t	*fragments;
	static unsigned		ncpus;
#if 1
	/* animated point light source */
	static float		r;

	r += .02;

	scene.lights[0].light.emitter.point.center.x = cosf(r) * 4.5f;
	scene.lights[0].light.emitter.point.center.z = sinf(r * 3.0f) * 4.5f;

	/* move the camera in a circle */
	camera.position.x = sinf(r) * (cosf(r) * 2.0f + 5.0f);
	camera.position.z = cosf(r) * (cosf(r) * 2.0f + 5.0f);

	/* also move up and down */
	camera.position.y = cosf(r * 1.3f) * 4.0f + 2.08f;

	/* keep camera facing the origin */
	camera.orientation.yaw = r;

	/* tilt camera pitch in time with up and down movements, phase shifted appreciably */
	camera.orientation.pitch = sinf((M_PI * 1.5f) + r * 1.3f) * .6f + -.35f;
#endif

	if (!initialized) {
		initialized = 1;
		ncpus = get_ncpus();

		if (ncpus > 1) {
			threads = ray_threads_create(ncpus - 1);
			fragments = malloc(sizeof(fb_fragment_t) * ncpus);
		}
	}

	if (ncpus > 1) {
		/* Always recompute the fragments[] geometry.
		 * This way the fragment geometry can change at any moment and things will
		 * continue functioning, which may prove important later on.
		 * (imagine things like a preview window, or perhaps composite modules
		 * which call on other modules supplying virtual fragments of varying dimensions..)
		 */
		fb_fragment_divide(fragment, ncpus, fragments);
	} else {
		fragments = fragment;
	}

	ray_scene_render_fragments(&scene, &camera, threads, fragments);
}


rototiller_renderer_t	ray_renderer = {
	.render = ray,
	.name = "ray",
	.description = "Multi-threaded ray tracer",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "GPLv2",
};
