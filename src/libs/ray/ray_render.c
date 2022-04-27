#include <stdlib.h>
#include <math.h>

#include "til_fb.h"

#include "ray_camera.h"
#include "ray_color.h"
#include "ray_gamma.h"
#include "ray_render_object.h"
#include "ray_ray.h"
#include "ray_scene.h"

#define MAX_RECURSION_DEPTH	4
#define MIN_RELEVANCE		0.05f

typedef struct ray_render_t {
	const ray_scene_t	*scene;		/* scene being rendered */
	const ray_camera_t	*camera;	/* camera rendering the scene */

	ray_color_t		ambient_light;
	ray_camera_frame_t	frame;
	ray_gamma_t		gamma;

	ray_render_object_t	objects[];
} ray_render_t;

/* Determine if the ray is obstructed by an object within the supplied distance, for shadows */
static inline int ray_is_obstructed(ray_render_t *render, unsigned depth, ray_ray_t *ray, float distance)
{
	ray_render_object_t	*object;

	for (object = render->objects; object->type; object++) {
		float	ood;

		if (ray_render_object_intersects_ray(object, depth, ray, &ood) &&
		    ood < distance) {
			return 1;
		}
	}

	return 0;
}


/* shadow test */
static inline int point_is_shadowed(ray_render_t *render, unsigned depth, ray_3f_t *light_direction, float distance, ray_3f_t *point)
{
	ray_ray_t	shadow_ray;

	shadow_ray.direction = *light_direction;
	shadow_ray.origin = *point;

	if (ray_is_obstructed(render, depth + 1, &shadow_ray, distance))
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
static inline ray_color_t shade_intersection(ray_render_t *render, ray_render_object_t *object, ray_ray_t *ray, ray_3f_t *intersection, ray_3f_t *normal, unsigned depth, float *res_reflectivity)
{
	ray_surface_t	surface = ray_render_object_surface(object, intersection);
	ray_color_t	color = ray_3f_mult(&surface.color, &render->ambient_light);
	ray_object_t	*light;

	/* visit lights for shadows and illumination */
	for (light = render->scene->lights; light->type; light++) {
		ray_3f_t	lvec = ray_3f_sub(&light->light.emitter.point.center, intersection);
		float		ldist = ray_3f_length(&lvec);
		float		lvec_normal_dot;

		lvec = ray_3f_mult_scalar(&lvec, (1.0f / ldist)); /* normalize lvec */
#if 1
		if (point_is_shadowed(render, depth, &lvec, ldist, intersection))
			continue;
#endif
		lvec_normal_dot = ray_3f_dot(normal, &lvec);

		if (lvec_normal_dot > 0) {
#if 1
			float		rvec_lvec_dot = ray_3f_dot(&ray->direction, &lvec);
			float		intensity = light->light.brightness * (1.0 / (ldist * ldist));
			ray_color_t	diffuse;

			diffuse = ray_3f_mult_scalar(&surface.color, lvec_normal_dot);
			diffuse = ray_3f_mult_scalar(&diffuse, surface.diffuse);
			color = ray_3f_add(&color, &diffuse);

			if (rvec_lvec_dot > 0) {
				ray_color_t	specular;

				/* FIXME: assumes light is a point for its color */
				specular = ray_3f_mult_scalar(&light->light.emitter.point.surface.color, approx_powf(rvec_lvec_dot, surface.highlight_exponent));
				specular = ray_3f_mult_scalar(&specular, surface.specular);
				color = ray_3f_add(&color, &specular);
			}

			color = ray_3f_mult_scalar(&color, intensity);
#else
			ray_color_t	diffuse;

			diffuse = ray_3f_mult_scalar(&surface.color, lvec_normal_dot);
			color = ray_3f_add(&color, &diffuse);
#endif
		}
	}

	/* for now just treat specular as the reflectivity */
	*res_reflectivity = surface.specular;

	return color;
}


static inline ray_render_object_t * find_nearest_intersection(ray_render_t *render, ray_render_object_t *reflector, ray_ray_t *ray, unsigned depth, float *res_distance)
{
	ray_render_object_t	*nearest_object = NULL;
	float			nearest_object_distance = INFINITY;
	ray_render_object_t	*object;

	for (object = render->objects; object->type; object++) {
		float		distance;

		/* Don't bother checking if a reflected ray intersects the object reflecting it,
		 * reflector = NULL for primary rays, which will never compare as true here. */
		if (object == reflector)
			continue;

		/* Does this ray intersect object? */
		if (ray_render_object_intersects_ray(object, depth, ray, &distance)) {
			/* Is it the nearest intersection? */
			if (distance < nearest_object_distance) {
				nearest_object = object;
				nearest_object_distance = distance;
			}
		}
	}

	if (nearest_object)
		*res_distance = nearest_object_distance;

	return nearest_object;
}


static inline ray_color_t trace_ray(ray_render_t *render, ray_ray_t *primary_ray)
{
	ray_color_t		color = { .x = 0.0f, .y = 0.0f, .z = 0.0f };
	ray_3f_t		intersection, normal;
	ray_render_object_t	*reflector = NULL;
	float			relevance = 1.0f, reflectivity;
	unsigned		depth = 0;
	ray_ray_t		reflected_ray, *ray = primary_ray;

	do {
		ray_render_object_t	*nearest_object;
		float			nearest_distance;

		if (reflector) {
			float		dot = ray_3f_dot(&ray->direction, &normal);
			ray_3f_t	new_direction = ray_3f_mult_scalar(&normal, dot * 2.0f);

			new_direction = ray_3f_sub(&ray->direction, &new_direction);

			reflected_ray.origin = intersection;
			reflected_ray.direction = new_direction;

			ray = &reflected_ray;
		}

		nearest_object = find_nearest_intersection(render, reflector, ray, depth, &nearest_distance);
		if (nearest_object) {
			ray_3f_t	more_color;
			ray_3f_t	rvec;

			rvec = ray_3f_mult_scalar(&ray->direction, nearest_distance);
			intersection = ray_3f_add(&ray->origin, &rvec);
			normal = ray_render_object_normal(nearest_object, &intersection);

			more_color = shade_intersection(render, nearest_object, ray, &intersection, &normal, depth, &reflectivity);
			more_color = ray_3f_mult_scalar(&more_color, relevance);
			color = ray_3f_add(&color, &more_color);
		}

		reflector = nearest_object;
	} while (reflector && (++depth < MAX_RECURSION_DEPTH) && (relevance *= reflectivity) >= MIN_RELEVANCE);

	return color;
}


void ray_render_trace_fragment(ray_render_t *render, til_fb_fragment_t *fb_fragment)
{
	uint32_t		*buf = fb_fragment->buf;
	ray_camera_fragment_t	fragment;
	ray_ray_t		ray;

	ray_camera_fragment_begin(&render->frame, fb_fragment, &ray, &fragment);
	do {
		do {
			*buf = ray_gamma_color_to_uint32_rgb(&render->gamma, trace_ray(render, &ray));
			buf++;
		} while (ray_camera_fragment_x_step(&fragment));

		buf += fb_fragment->stride;
	} while (ray_camera_fragment_y_step(&fragment));
}


/* prepare the scene for rendering with camera, must be called whenever anything in the scene+camera pair has been changed. */
/* this is basically a time for the raytracer to precompute whatever it can which otherwise ends up occurring per-ray */
/* the camera is included so primary rays which all have a common origin may be optimized for */
ray_render_t * ray_render_new(const ray_scene_t *scene, const ray_camera_t *camera, unsigned frame_width, unsigned frame_height)
{
	ray_render_t	*render;
	ray_object_t	*object;
	unsigned	i;

	for (i = 0, object = scene->objects; object->type; object++)
		i++;

	render = malloc(sizeof(ray_render_t) + (i + 1) * sizeof(ray_render_object_t));
	if (!render)
		return NULL;

	render->scene = scene;
	render->camera = camera;

	render->ambient_light = ray_3f_mult_scalar(&scene->ambient_color, scene->ambient_brightness);
	ray_gamma_prepare(scene->gamma, &render->gamma);
	ray_camera_frame_prepare(camera, frame_width, frame_height, &render->frame);

	for (i = 0, object = scene->objects; object->type; object++)
		render->objects[i++] = ray_render_object_prepare(object, camera);

	render->objects[i].type = RAY_OBJECT_TYPE_SENTINEL;

	return render;
}


void ray_render_free(ray_render_t *render)
{
	free(render);
}
