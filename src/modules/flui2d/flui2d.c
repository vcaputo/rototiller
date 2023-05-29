#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "til.h"
#include "til_fb.h"
#include "til_module_context.h"
#include "til_settings.h"
#include "til_stream.h"
#include "til_tap.h"


/* This code is almost entirely taken from the paper:
 * Real-Time Fluid Dynamics for Games
 * Jos Stam - Alias | Wavefront
 *
 * I take zero credit for it, I only wrote the rototiller integration.
 *   - Vito Caputo <vcaputo@pengaru.com> 10/13/2019
 */

#define ROOT		128	// Change this to vary the density field resolution
#define SIZE		((ROOT + 2) * (ROOT + 2))
#define IX(i, j)	((i) + (ROOT + 2) * (j))
#define SWAP(x0, x)	{float *tmp = x0; x0 = x; x = tmp;}

typedef struct flui2d_t {
	float	u[SIZE], v[SIZE], u_prev[SIZE], v_prev[SIZE];
	float	dens_r[SIZE], dens_prev_r[SIZE];
	float	dens_g[SIZE], dens_prev_g[SIZE];
	float	dens_b[SIZE], dens_prev_b[SIZE];
	float	visc, diff, decay;
} flui2d_t;

static void set_bnd(int N, int b, float *x)
{
	for (int i = 1; i <= N; i++) {
		x[IX(0, i)] = b == 1 ? -x[IX(1, i)] : x[IX(1, i)];
		x[IX(N + 1, i)] = b == 1 ? -x[IX(N, i)] : x[IX(N, i)];
		x[IX(i, 0)] = b == 2 ? -x[IX(i, 1)] : x[IX(i, 1)];
		x[IX(i, N + 1)] = b == 2 ? -x[IX(i, N)] : x[IX(i, N)];
	}

	x[IX(0 , 0)] = 0.5 * (x[IX(1, 0)] + x[IX(0, 1)]);
	x[IX(0 , N + 1)] = 0.5 * (x[IX(1, N + 1)] + x[IX(0, N)]);
	x[IX(N + 1, 0)] = 0.5 * (x[IX(N, 0)] + x[IX(N + 1, 1)]);
	x[IX(N + 1, N + 1)] = 0.5 * (x[IX(N, N + 1)] + x[IX(N + 1, N)]);
}

static void add_source(int N, float *x, float *s, float dt)
{
	int	size = (N + 2) * (N + 2);

	for (int i = 0; i < size; i++)
		x[i] += dt * s[i];
}

static void diffuse(int N, int b, float *x, float *x0, float diff, float decay, float dt)
{
	float a = dt * diff * (float)N * (float)N;
	int i, j, k;
	float z = 1.f / (1.f + 4.f * a);

	for (k = 0; k < 20; k++) {
		for (i = 1; i <= N; i++) {
			for (j = 1; j <= N; j++) {
				x[IX(i, j)] = (x0[IX(i, j)] + a * (x[IX(i - 1, j)] + x[IX(i + 1, j)] + x[IX(i, j - 1)] + x[IX(i, j + 1)])) * z * (1.f - decay);
			}
		}
		set_bnd(N, b, x);
	}
}

static void advect(int N, int b, float *d, float *d0, float *u, float *v, float dt)
{
	float x, y, s0, t0, s1, t1, dt0;
	int i, j, i0, j0, i1, j1;

	dt0 = dt * (float)N;
	for (i = 1 ; i <= N ; i++) {
		for (j = 1 ; j <= N; j++) {
			x = (float)i - dt0 * u[IX(i, j)];
			y = (float)j - dt0 * v[IX(i, j)];

			if (x < .5f)
				x = .5f;
			if (x > (float)N + .5f)
				x = (float)N + .5f;

			i0 = (int)x;
			i1 = i0 + 1;

			if (y < .5f)
				y = .5f;
			if (y > (float)N + .5f)
				y = (float)N + .5f;

			j0 = (int)y;
			j1 = j0 + 1;

			s1 = x - (float)i0;
			s0 = 1.f - s1;
			t1 = y - (float)j0;
			t0 = 1.f - t1;

			d[IX(i, j)] = s0 * (t0 * d0[IX(i0, j0)] + t1 * d0[IX(i0, j1)]) + s1 * (t0 * d0[IX(i1, j0)] + t1 * d0[IX(i1, j1)]);
		}
	}
	set_bnd(N, b, d);
}

static void project(int N, float *u, float *v, float *p, float *div)
{
	float h = 1.f / (float)N;
	int i, j, k;

	for (i = 1; i <= N ; i++) {
		for (j = 1; j <= N; j++) {
			div[IX(i, j)] = -0.5 * h *(u[IX(i + 1, j)] - u[IX(i - 1, j)] + v[IX(i, j + 1)] - v[IX(i, j - 1)]);
			p[IX(i, j)] = 0;
		}
	}

	set_bnd(N, 0, div);
	set_bnd(N, 0, p);

	for (k = 0; k < 20; k++) {
		for (i = 1; i <= N; i++) {
			for (j = 1; j <= N; j++) {
				p[IX(i, j)] = (div[IX(i, j)] + p[IX(i - 1, j)] + p[IX(i + 1, j)] + p[IX(i, j - 1)] + p[IX(i, j + 1)]) * .25f;
			}
		}
		set_bnd(N, 0, p);
	}

	for (i = 1; i <= N; i++) {
		for (j = 1; j <= N; j++) {
			u[IX(i, j)] -= 0.5 * (p[IX(i + 1, j)] - p[IX(i - 1, j)]) / h;
			v[IX(i, j)] -= 0.5 * (p[IX(i, j + 1)] - p[IX(i, j - 1)]) / h;
		}
	}

	set_bnd(N, 1, u);
	set_bnd(N, 2, v);
}

static void dens_step(int N, float *x, float *x0, float *u, float *v, float diff, float decay, float dt)
{

	/*
	 * The paper includes this, but it blows up the simulation.
	 * add_source(N, x, x0, dt);
	 * SWAP(x0, x);
	 */
	diffuse(N, 0, x, x0, diff, decay, dt);
	SWAP(x0, x);
	advect(N, 0, x, x0, u, v, dt);
}

static void vel_step(int N, float *u, float *v, float *u0, float *v0, float visc, float dt)
{
	add_source(N, u, u0, dt);
	add_source(N, v, v0, dt);
	SWAP(u0, u);
	diffuse(N, 1, u, u0, visc, 0.f, dt);
	SWAP(v0, v);
	diffuse(N, 2, v, v0, visc, 0.f, dt);
	project(N, u, v, u0, v0);
	SWAP(u0, u);
	SWAP(v0, v);
	advect(N, 1, u, u0, u0, v0, dt);
	advect(N, 2, v, v0, u0, v0, dt);
	project(N, u, v, u0, v0);
}

typedef enum flui2d_emitters_t {
	FLUI2D_EMITTERS_FIGURE8 = 0,	/* this is the original/classic figure eight */
	FLUI2D_EMITTERS_CLOCKGRID,
} flui2d_emitters_t;

typedef struct flui2d_setup_t {
	til_setup_t		til_setup;
	float			viscosity;
	float			diffusion;
	float			decay;
	flui2d_emitters_t	emitters;
	float			clockstep;
} flui2d_setup_t;

typedef struct flui2d_context_t {
	til_module_context_t	til_module_context;
	flui2d_setup_t		*setup;

	struct {
		til_tap_t	viscosity, diffusion, decay;
	} taps;

	struct {
		float		viscosity, diffusion, decay;
	} vars;

	float			*viscosity, *diffusion, *decay;

	flui2d_t		fluid;
	float			xf, yf;
} flui2d_context_t;

#define FLUI2D_DEFAULT_EMITTERS		FLUI2D_EMITTERS_FIGURE8
#define FLUI2D_DEFAULT_CLOCKSTEP	.5

	/* These knobs affect how the simulated fluid behaves */
#define FLUI2D_DEFAULT_VISCOSITY	.000000001
#define FLUI2D_DEFAULT_DIFFUSION	.00001
#define FLUI2D_DEFAULT_DECAY		.0001


/* gamma correction derived from libs/ray/ray_gamma.[ch] */
static uint8_t	gamma_table[1024];


static inline uint32_t gamma_color_to_uint32_rgb(float r, float g, float b) {
	uint32_t	pixel;

	if (r > 1.0f)
		r = 1.0f;

	if (g > 1.0f)
		g = 1.0f;

	if (b > 1.0f)
		b = 1.0f;

	pixel = (uint32_t)gamma_table[(unsigned)floorf(1023.0f * r)];
	pixel <<= 8;
	pixel |= (uint32_t)gamma_table[(unsigned)floorf(1023.0f * g)];
	pixel <<= 8;
	pixel |= (uint32_t)gamma_table[(unsigned)floorf(1023.0f * b)];

	return pixel;
}


static void gamma_init(float gamma)
{
	/* This is from graphics gems 2 "REAL PIXELS" */
	for (unsigned i = 0; i < 1024; i++)
		gamma_table[i] = 256.0f * powf((((float)i + .5f) / 1024.0f), 1.0f/gamma);
}


static til_module_context_t * flui2d_create_context(const til_module_t *module, til_stream_t *stream, unsigned seed, unsigned ticks, unsigned n_cpus, char *path, til_setup_t *setup)
{
	static int		initialized;
	flui2d_context_t	*ctxt;

	ctxt = til_module_context_new(module, sizeof(flui2d_context_t), stream, seed, ticks, n_cpus, path, setup);
	if (!ctxt)
		return NULL;

	if (!initialized) {
		initialized = 1;
		gamma_init(1.4f);
	}

	ctxt->setup = (flui2d_setup_t *)setup;

	ctxt->taps.viscosity = til_tap_init_float(ctxt, &ctxt->viscosity, 1, &ctxt->vars.viscosity, "viscosity");
	ctxt->taps.diffusion = til_tap_init_float(ctxt, &ctxt->diffusion, 1, &ctxt->vars.diffusion, "diffusion");
	ctxt->taps.decay = til_tap_init_float(ctxt, &ctxt->decay, 1, &ctxt->vars.decay, "decay");

	return &ctxt->til_module_context;
}


/* Prepare a frame for concurrent drawing of fragment using multiple fragments */
static void flui2d_prepare_frame(til_module_context_t *context, til_stream_t *stream, unsigned ticks, til_fb_fragment_t **fragment_ptr, til_frame_plan_t *res_frame_plan)
{
	flui2d_context_t	*ctxt = (flui2d_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	float	r = (ticks % (unsigned)(2 * M_PI * 1000)) * .001f;

	*res_frame_plan = (til_frame_plan_t){ .fragmenter = til_fragmenter_tile64 };

	if (!til_stream_tap_context(stream, context, NULL, &ctxt->taps.viscosity))
		*ctxt->viscosity = ctxt->setup->viscosity;

	if (!til_stream_tap_context(stream, context, NULL, &ctxt->taps.diffusion))
		*ctxt->diffusion = ctxt->setup->diffusion;

	if (!til_stream_tap_context(stream, context, NULL, &ctxt->taps.decay))
		*ctxt->decay = ctxt->setup->decay;

	/* this duplication of visc/diff/decay is silly, it's just a product of this
	 * module being written as a flui2d_t class in-situ but distinct from the module.
	 */
	ctxt->fluid.visc = *ctxt->viscosity;
	ctxt->fluid.diff = *ctxt->diffusion;
	ctxt->fluid.decay = *ctxt->decay;

	switch (ctxt->setup->emitters) {
	case FLUI2D_EMITTERS_FIGURE8: {
		int	x = (cos(r) * .4f + .5f) * (float)ROOT;	/* figure eight pattern for the added densities */
		int	y = (sin(r * 2.f) * .4f + .5f) * (float)ROOT;

		ctxt->fluid.dens_prev_r[IX(x, y)] = .5f + cos(r) * .5f;
		ctxt->fluid.dens_prev_g[IX(x, y)] = .5f + sin(r) * .5f;
		ctxt->fluid.dens_prev_b[IX(x, y)] = .5f + cos(r * 2.f) * .5f;

		/* This orientation for the added velocities at the added densities isn't trying to
		 * emulate any sort of physical relationship to the movement - it's just creating a variety
		 * of turbulence.  It'd be trivial to make it look like a rocket's jetstream or something.
		 */
		ctxt->fluid.u_prev[IX(x, y)] = cos(r * 3.f) * 10.f;
		ctxt->fluid.v_prev[IX(x, y)] = sin(r * 3.f) * 10.f;
		break;
	}

	case FLUI2D_EMITTERS_CLOCKGRID: {
#define FLUI2D_CLOCKGRID_SIZE	(ROOT>>4)
#define FLUI2D_CLOCKGRID_STEP	(ROOT/FLUI2D_CLOCKGRID_SIZE)
		for (int y = FLUI2D_CLOCKGRID_STEP; y < ROOT; y += FLUI2D_CLOCKGRID_STEP) {
			for (int x = FLUI2D_CLOCKGRID_STEP; x < ROOT; x += FLUI2D_CLOCKGRID_STEP, r += ctxt->setup->clockstep * M_PI * 2) {

				ctxt->fluid.dens_prev_r[IX(x, y)] = .5f + cos(r) * .5f;
				ctxt->fluid.dens_prev_g[IX(x, y)] = .5f + sin(r) * .5f;
				ctxt->fluid.dens_prev_b[IX(x, y)] = .5f + cos(r * 2.f) * .5f;

				ctxt->fluid.u_prev[IX(x, y)] = cos(r * 3.f);
				ctxt->fluid.v_prev[IX(x, y)] = sin(r * 3.f);
			}
		}
		break;
	}
	}

	/* These are the core of the simulation, and can't currently be threaded using the paper's implementation, so they
	 * must occur serialized here in prepare_frame.  It would be interesting to try refactor the API and tweak the
	 * implementation for threading, as it would really open up larger field sizes as well as map more naturally to
	 * a GLSL implementation for a fragment shader.
	 */
	vel_step(ROOT, ctxt->fluid.u, ctxt->fluid.v, ctxt->fluid.u_prev, ctxt->fluid.v_prev, ctxt->fluid.visc, .1f);
	dens_step(ROOT, ctxt->fluid.dens_r, ctxt->fluid.dens_prev_r, ctxt->fluid.u, ctxt->fluid.v, ctxt->fluid.diff, ctxt->fluid.decay, .1f);
	dens_step(ROOT, ctxt->fluid.dens_g, ctxt->fluid.dens_prev_g, ctxt->fluid.u, ctxt->fluid.v, ctxt->fluid.diff, ctxt->fluid.decay, .1f);
	dens_step(ROOT, ctxt->fluid.dens_b, ctxt->fluid.dens_prev_b, ctxt->fluid.u, ctxt->fluid.v, ctxt->fluid.diff, ctxt->fluid.decay, .1f);

	ctxt->xf = 1.f / fragment->frame_width;
	ctxt->yf = 1.f / fragment->frame_height;
}


/* Draw a the flui2d densities */
static void flui2d_render_fragment(til_module_context_t *context, til_stream_t *stream, unsigned ticks, unsigned cpu, til_fb_fragment_t **fragment_ptr)
{
	flui2d_context_t	*ctxt = (flui2d_context_t *)context;
	til_fb_fragment_t	*fragment = *fragment_ptr;

	for (int y = fragment->y; y < fragment->y + fragment->height; y++) {
		int	y0, y1;
		float	Y;

		Y = (float)y * ctxt->yf * (float)ROOT;
		y0 = (int)Y;
		y1 = y0 + 1;

		for (int x = fragment->x; x < fragment->x + fragment->width; x++) {
			float		X, dens, dx0, dx1;
			int		x0, x1;
			float		r, g, b;

			X = (float)x * ctxt->xf * (float)ROOT;
			x0 = (int)X;
			x1 = x0 + 1;

			/* linear interpolation of density samples */
			dx0 = ctxt->fluid.dens_r[(int)IX(x0, y0)] * (1.f - (X - x0));
			dx0 += ctxt->fluid.dens_r[(int)IX(x1, y0)] * (X - x0);
			dx1 = ctxt->fluid.dens_r[(int)IX(x0, y1)] * (1.f - (X - x0));
			dx1 += ctxt->fluid.dens_r[(int)IX(x1, y1)] * (X - x0);
			r = dx0 * (1.f - (Y - y0)) + dx1 * (Y - y0);

			dx0 = ctxt->fluid.dens_g[(int)IX(x0, y0)] * (1.f - (X - x0));
			dx0 += ctxt->fluid.dens_g[(int)IX(x1, y0)] * (X - x0);
			dx1 = ctxt->fluid.dens_g[(int)IX(x0, y1)] * (1.f - (X - x0));
			dx1 += ctxt->fluid.dens_g[(int)IX(x1, y1)] * (X - x0);
			g = dx0 * (1.f - (Y - y0)) + dx1 * (Y - y0);

			dx0 = ctxt->fluid.dens_b[(int)IX(x0, y0)] * (1.f - (X - x0));
			dx0 += ctxt->fluid.dens_b[(int)IX(x1, y0)] * (X - x0);
			dx1 = ctxt->fluid.dens_b[(int)IX(x0, y1)] * (1.f - (X - x0));
			dx1 += ctxt->fluid.dens_b[(int)IX(x1, y1)] * (X - x0);
			b = dx0 * (1.f - (Y - y0)) + dx1 * (Y - y0);

			til_fb_fragment_put_pixel_unchecked(fragment, 0, x, y, gamma_color_to_uint32_rgb(r, g, b));
		}
	}
}


/* Settings hooks for configurable variables */
static int flui2d_setup(const til_settings_t *settings, til_setting_t **res_setting, const til_setting_desc_t **res_desc, til_setup_t **res_setup)
{
	const char	*viscosity;
	const char	*diffusion;
	const char	*values[] = {
				".000000000001",
				".0000000001",
				".000000001",
				".00000001",
				".0000001",
				".000001",
				".00001",
				".0001",
				NULL
			};
	const char	*decay;
	const char	*decay_values[] = {
				".000001",
				".00001",
				".0001",
				".001",
				".01",
				NULL
			};
	const char	*emitters;
	const char	*emitters_values[] = {
				"figure8",
				"clockgrid",
				NULL
			};
	const char	*clockstep;
	const char	*clockstep_values[] = {
				".05",
				".1",
				".25",
				".33",
				".5",
				".66",
				".75",
				".99",
				NULL
			};
	int		r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Fluid viscosity",
							.key = "viscosity",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(FLUI2D_DEFAULT_VISCOSITY),
							.values = values,
							.annotations = NULL
						},
						&viscosity,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Fluid diffusion",
							.key = "diffusion",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(FLUI2D_DEFAULT_DIFFUSION),
							.values = values,
							.annotations = NULL
						},
						&diffusion,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Fluid decay",
							.key = "decay",
							.regex = "\\.[0-9]+",
							.preferred = TIL_SETTINGS_STR(FLUI2D_DEFAULT_DECAY),
							.values = decay_values,
							.annotations = NULL
						},
						&decay,
						res_setting,
						res_desc);
	if (r)
		return r;

	r = til_settings_get_and_describe_value(settings,
						&(til_setting_spec_t){
							.name = "Fluid emitters style",
							.key = "emitters",
							.regex = "^(figure8|clockgrid)",
							.preferred = emitters_values[FLUI2D_DEFAULT_EMITTERS],
							.values = emitters_values,
							.annotations = NULL
						},
						&emitters,
						res_setting,
						res_desc);
	if (r)
		return r;

	if (!strcasecmp(emitters, "clockgrid")) {
		r = til_settings_get_and_describe_value(settings,
							&(til_setting_spec_t){
								.name = "Fluid clockgrid emitters clock step",
								.key = "clockstep",
								.regex = "\\.[0-9]+",
								.preferred = TIL_SETTINGS_STR(FLUI2D_DEFAULT_CLOCKSTEP),
								.values = clockstep_values,
								.annotations = NULL
							},
							&clockstep,
							res_setting,
							res_desc);
		if (r)
			return r;
	}

	if (res_setup) {
		flui2d_setup_t	*setup;

		setup = til_setup_new(settings, sizeof(*setup), NULL);
		if (!setup)
			return -ENOMEM;

		/* TODO: return -EINVAL on parse errors? */
		sscanf(viscosity, "%f", &setup->viscosity);
		sscanf(diffusion, "%f", &setup->diffusion);
		sscanf(decay, "%f", &setup->decay);

		/* prevent overflow in case an explicit out of range setting is supplied */
		if (setup->decay > 1.f || setup->decay < 0.f) {
			free(setup);
			return -EINVAL;
		}

		for (int i = 0; emitters_values[i]; i++) {
			if (!strcasecmp(emitters, emitters_values[i])) {
				setup->emitters = i;

				break;
			}
		}

		if (setup->emitters == FLUI2D_EMITTERS_CLOCKGRID)
			 sscanf(clockstep, "%f", &setup->clockstep);

		*res_setup = &setup->til_setup;
	}

	return 0;
}


til_module_t	flui2d_module = {
	.create_context = flui2d_create_context,
	.prepare_frame = flui2d_prepare_frame,
	.render_fragment = flui2d_render_fragment,
	.setup = flui2d_setup,
	.name = "flui2d",
	.description = "Fluid dynamics simulation in 2D (threaded (poorly))",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
};
