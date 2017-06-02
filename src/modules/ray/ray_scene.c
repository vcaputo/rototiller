#include <stdlib.h>
#include <math.h>

#include "fb.h"

#include "ray_camera.h"
#include "ray_color.h"
#include "ray_object.h"
#include "ray_ray.h"
#include "ray_scene.h"

#define MAX_RECURSION_DEPTH	4


static ray_color_t trace_ray(ray_scene_t *scene, ray_object_t *reflector, ray_ray_t *ray, unsigned depth);


/* Determine if the ray is obstructed by an object within the supplied distance, for shadows */
static inline int ray_is_obstructed(ray_scene_t *scene, unsigned depth, ray_ray_t *ray, float distance)
{
	unsigned	i;

	for (i = 0; i < scene->n_objects; i++) {
		float	ood;

		if (ray_object_intersects_ray(&scene->objects[i], depth, ray, &ood) &&
		    ood < distance) {
			return 1;
		}
	}

	return 0;
}


/* shadow test */
static inline int point_is_shadowed(ray_scene_t *scene, unsigned depth, ray_3f_t *light_direction, float distance, ray_3f_t *point)
{
	ray_ray_t	shadow_ray;

	/* negate the light vector so it's pointed at the light rather than from it */
	shadow_ray.direction = ray_3f_negate(light_direction);

	/* we must shift the origin slightly (epsilon) towards the light to
	 * prevent spurious self-obstruction at the ray:object intersection */
	shadow_ray.origin = ray_3f_mult_scalar(&shadow_ray.direction, 0.00001f);
	shadow_ray.origin = ray_3f_add(&shadow_ray.origin, point);

	if (ray_is_obstructed(scene, depth + 1, &shadow_ray, distance))
		return 1;

	return 0;
}


/* a faster powf() that's good enough for our purposes.
 * XXX: note there's a faster technique which exploits the IEEE floating point format:
 * https://github.com/ekmett/approximate/blob/master/cbits/fast.c#L185
 */
static inline float approx_powf(float x, float y)
{
	return expf(y * logf(x));
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
	color = ray_3f_mult(&surface.color, &scene->_prepared.ambient_light);

	/* visit lights for shadows and illumination */
	for (i = 0; i < scene->n_lights; i++) {
		ray_3f_t	lvec = ray_3f_sub(&scene->lights[i].light.emitter.point.center, &intersection);
		float		ldist = ray_3f_length(&lvec);
		float		lvec_normal_dot;

		lvec = ray_3f_mult_scalar(&lvec, (1.0f / ldist)); /* normalize lvec */
#if 1
		if (point_is_shadowed(scene, depth, &lvec, ldist, &intersection))
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

			if (rvec_lvec_dot > 0) {
				/* FIXME: assumes light is a point for its color */
				specular = ray_3f_mult_scalar(&scene->lights[i].light.emitter.point.surface.color, approx_powf(rvec_lvec_dot, surface.highlight_exponent));
				specular = ray_3f_mult_scalar(&specular, surface.specular);
				color = ray_3f_add(&color, &specular);
			}
#else
			ray_color_t	diffuse;

			diffuse = ray_3f_mult_scalar(&surface.color, lvec_normal_dot);
			color = ray_3f_add(&color, &diffuse);
#endif
		}
	}

	/* generate a reflection ray */
#if 1
	if (depth < MAX_RECURSION_DEPTH) {
		float		dot = ray_3f_dot(&ray->direction, &normal);
		ray_ray_t	reflected_ray = { .direction = ray_3f_mult_scalar(&normal, dot * 2.0f) };
		ray_3f_t	reflection;

		reflected_ray.origin = intersection;
		reflected_ray.direction = ray_3f_sub(&ray->direction, &reflected_ray.direction);

		reflection = trace_ray(scene, object, &reflected_ray, depth);
		reflection = ray_3f_mult_scalar(&reflection, surface.specular);
		color = ray_3f_add(&color, &reflection);
	}
#endif

	/* TODO: generate a refraction ray */

	return color;
}


static ray_color_t trace_ray(ray_scene_t *scene, ray_object_t *reflector, ray_ray_t *ray, unsigned depth)
{
	ray_object_t	*nearest_object = NULL;
	float		nearest_object_distance = INFINITY;
	ray_color_t	color = { .x = 0.0, .y = 0.0, .z = 0.0 };
	unsigned	i;

	depth++;

	for (i = 0; i < scene->n_objects; i++) {
		ray_object_t	*object = &scene->objects[i];
		float		distance;

		/* Don't bother checking if a reflected ray intersects the object reflecting it,
		 * reflector = NULL for primary rays, which will never compare as true here. */
		if (object == reflector)
			continue;

		/* Does this ray intersect object? */
		if (ray_object_intersects_ray(object, depth, ray, &distance)) {

			/* Is it the nearest intersection? */
			if (distance < nearest_object_distance) {
				nearest_object = object;
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
			*buf = ray_color_to_uint32_rgb(trace_ray(scene, NULL, &ray, 0));
			buf++;
		} while (ray_camera_frame_x_step(&frame));

		buf += stride;
	} while (ray_camera_frame_y_step(&frame));
}


/* prepare the scene for rendering with camera, must be called whenever anything in the scene+camera pair has been changed. */
/* this is basically a time for the raytracer to precompute whatever it can which otherwise ends up occurring per-ray */
/* the camera is included so primary rays which all have a common origin may be optimized for */
void ray_scene_prepare(ray_scene_t *scene, ray_camera_t *camera)
{
	unsigned	i;

	scene->_prepared.ambient_light = ray_3f_mult_scalar(&scene->ambient_color, scene->ambient_brightness);

	for (i = 0; i < scene->n_objects; i++)
		ray_object_prepare(&scene->objects[i], camera);
}
