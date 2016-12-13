#include <stdlib.h>
#include <math.h>

#include "fb.h"

#include "ray_camera.h"
#include "ray_color.h"
#include "ray_object.h"
#include "ray_ray.h"
#include "ray_scene.h"
#include "ray_threads.h"

#define MAX_RECURSION_DEPTH	5


static ray_color_t trace_ray(ray_scene_t *scene, ray_ray_t *ray, unsigned depth);


/* Determine if the ray is obstructed by an object within the supplied distance, for shadows */
static inline int ray_is_obstructed(ray_scene_t *scene, ray_ray_t *ray, float distance)
{
	unsigned	i;

	for (i = 0; i < scene->n_objects; i++) {
		float	ood;

		if (scene->objects[i].type == RAY_OBJECT_TYPE_LIGHT)
			continue;

		if (ray_object_intersects_ray(&scene->objects[i], ray, &ood) &&
		    ood < distance) {
			return 1;
		}
	}

	return 0;
}


/* Determine the color @ distance on ray on object viewed from origin */
static inline ray_color_t shade_ray(ray_scene_t *scene, ray_ray_t *ray, ray_object_t *object, float distance, unsigned depth)
{
	ray_surface_t	surface;
	ray_color_t	color;
	ray_3f_t	rvec = ray_3f_mult_scalar(&ray->direction, distance);
	ray_3f_t	intersection = ray_3f_sub(&ray->origin, &rvec);
	ray_3f_t	normal = ray_object_normal(object, &intersection);
	unsigned	i;

	surface = ray_object_surface(object, &intersection);
	color = ray_3f_mult_scalar(&scene->ambient_color, scene->ambient_brightness);
	color = ray_3f_mult(&surface.color, &color);

	/* visit lights for shadows and illumination */
	for (i = 0; i < scene->n_lights; i++) {
		ray_3f_t	lvec = ray_3f_sub(&scene->lights[i].light.emitter.point.center, &intersection);
		float		ldist = ray_3f_length(&lvec);
		float		lvec_normal_dot;
		ray_ray_t	shadow_ray;

		lvec = ray_3f_mult_scalar(&lvec, (1.0f / ldist)); /* normalize lvec */
#if 1
		/* skip this light if it's obstructed,
		 * we must shift the origin slightly towards the light to prevent
		 * spurious self-obstruction at the ray:object intersection */
		/* negate the light vector so it's pointed at the light rather than from it */
		shadow_ray.direction = ray_3f_negate(&lvec);
		shadow_ray.origin = ray_3f_mult_scalar(&shadow_ray.direction, 0.00001f);
		shadow_ray.origin = ray_3f_add(&shadow_ray.origin, &intersection);

		if (ray_is_obstructed(scene, &shadow_ray, ldist))
			continue;
#endif
		lvec_normal_dot = ray_3f_dot(&normal, &lvec);

		if (lvec_normal_dot > 0) {
#if 1
			float		rvec_lvec_dot = ray_3f_dot(&ray->direction, &lvec);
			ray_color_t	diffuse;
			ray_color_t	specular;

			diffuse = ray_3f_mult_scalar(&surface.color, lvec_normal_dot);
			diffuse = ray_3f_mult_scalar(&diffuse, surface.diffuse);
			color = ray_3f_add(&color, &diffuse);

			/* FIXME: assumes light is a point for its color, and 20 is a constant "Phong exponent",
			 * which should really be object/surface-specific
			 */
			specular = ray_3f_mult_scalar(&scene->lights[i].light.emitter.point.surface.color, powf(rvec_lvec_dot, 20));
			specular = ray_3f_mult_scalar(&specular, surface.specular);
			color = ray_3f_add(&color, &specular);
#else
			ray_color_t	diffuse;

			diffuse = ray_3f_mult_scalar(&surface.color, lvec_normal_dot);
			color = ray_3f_add(&color, &diffuse);
#endif
		}
	}

	/* generate a reflection ray */
#if 1
	float		dot = ray_3f_dot(&ray->direction, &normal);
	ray_ray_t	reflected_ray = { .direction = ray_3f_mult_scalar(&normal, dot * 2.0f) };
	ray_3f_t	reflection;

	reflected_ray.origin = intersection;
	reflected_ray.direction = ray_3f_sub(&ray->direction, &reflected_ray.direction);

	reflection = trace_ray(scene, &reflected_ray, depth + 1);
	reflection = ray_3f_mult_scalar(&reflection, surface.specular);
	color = ray_3f_add(&color, &reflection);
#endif

	/* TODO: generate a refraction ray */

	return color;
}


static ray_color_t trace_ray(ray_scene_t *scene, ray_ray_t *ray, unsigned depth)
{
	ray_object_t	*nearest_object = NULL;
	float		nearest_object_distance = INFINITY;
	ray_color_t	color = { .x = 0.0, .y = 0.0, .z = 0.0 };
	unsigned	i;

	depth++;
	if (depth > MAX_RECURSION_DEPTH)
		return color;

	for (i = 0; i < scene->n_objects; i++) {
		float	distance;

		/* Does this ray intersect object? */
		if (ray_object_intersects_ray(&scene->objects[i], ray, &distance)) {

			/* Is it the nearest intersection? */
			if (!nearest_object ||
			    distance < nearest_object_distance) {
				nearest_object = &scene->objects[i];
				nearest_object_distance = distance;
			}
		}
	}

	if (nearest_object)
		color = shade_ray(scene, ray, nearest_object, nearest_object_distance, depth);

	depth--;

	return color;
}


void ray_scene_render_fragment(ray_scene_t *scene, ray_camera_t *camera, fb_fragment_t *fragment)
{
	ray_camera_frame_t	frame;
	ray_ray_t		ray;
	uint32_t		*buf = fragment->buf;
	unsigned		stride = fragment->stride / 4;

	ray_camera_frame_begin(camera, fragment, &ray, &frame);
	do {
		do {
			*buf = ray_color_to_uint32_rgb(trace_ray(scene, &ray, 0));
			buf++;
		} while (ray_camera_frame_x_step(&frame));

		buf += stride;
	} while (ray_camera_frame_y_step(&frame));
}

/* we expect fragments[threads->n_threads + 1], or fragments[1] when threads == NULL */
void ray_scene_render_fragments(ray_scene_t *scene, ray_camera_t *camera, ray_threads_t *threads, fb_fragment_t *fragments)
{
	unsigned	n_threads = threads ? threads->n_threads + 1 : 1;
	unsigned	i;

	for (i = 1; i < n_threads; i++)
		ray_thread_fragment_submit(&threads->threads[i - 1], scene, camera, &fragments[i]);

	/* always render the zero fragment in-line */
	ray_scene_render_fragment(scene, camera, &fragments[0]);

	for (i = 1; i < n_threads; i++)
		ray_thread_wait_idle(&threads->threads[i - 1]);
}
