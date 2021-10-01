#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "til.h"
#include "til_fb.h"
#include "til_settings.h"


/* This code is almost entirely taken from the paper:
 * Real-Time Fluid Dynamics for Games
 * Jos Stam - Alias | Wavefront
 *
 * I take zero credit for it, I only wrote the rototiller integration.
 *   - Vito Caputo <vcaputo@pengaru.com> 10/13/2019
 */

#if 1
	/* These knobs affect how the simulated fluid behaves */
#define DEFAULT_VISCOSITY	.000000001f
#define DEFAULT_DIFFUSION	.00001f
#else
#define DEFAULT_VISCOSITY	.00001f
#define DEFAULT_DIFFUSION	.000001f
#endif

#define ROOT		128	// Change this to vary the density field resolution
#define SIZE		((ROOT + 2) * (ROOT + 2))
#define IX(i, j)	((i) + (ROOT + 2) * (j))
#define SWAP(x0, x)	{float *tmp = x0; x0 = x; x = tmp;}

static float	flui2d_viscosity = DEFAULT_VISCOSITY;
static float	flui2d_diffusion = DEFAULT_DIFFUSION;

typedef struct flui2d_t {
	float	u[SIZE], v[SIZE], u_prev[SIZE], v_prev[SIZE];
	float	dens[SIZE], dens_prev[SIZE];
	float	visc, diff;
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

static void diffuse(int N, int b, float *x, float *x0, float diff, float dt)
{
	float a = dt * diff * (float)N * (float)N;
	int i, j, k;
	float z = 1.f / (1.f + 4.f * a);

	for (k = 0; k < 20; k++) {
		for (i = 1; i <= N; i++) {
			for (j = 1; j <= N; j++) {
				x[IX(i, j)] = (x0[IX(i, j)] + a * (x[IX(i - 1, j)] + x[IX(i + 1, j)] + x[IX(i, j - 1)] + x[IX(i, j + 1)])) * z;
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

static void dens_step(int N, float *x, float *x0, float *u, float *v, float diff, float dt)
{

	/*
	 * The paper includes this, but it blows up the simulation.
	 * add_source(N, x, x0, dt);
	 * SWAP(x0, x);
	 */
	diffuse(N, 0, x, x0, diff, dt);
	SWAP(x0, x);
	advect(N, 0, x, x0, u, v, dt);
}

static void vel_step(int N, float *u, float *v, float *u0, float *v0, float visc, float dt)
{
	add_source(N, u, u0, dt);
	add_source(N, v, v0, dt);
	SWAP(u0, u);
	diffuse(N, 1, u, u0, visc, dt);
	SWAP(v0, v);
	diffuse(N, 2, v, v0, visc, dt);
	project(N, u, v, u0, v0);
	SWAP(u0, u);
	SWAP(v0, v);
	advect(N, 1, u, u0, u0, v0, dt);
	advect(N, 2, v, v0, u0, v0, dt);
	project(N, u, v, u0, v0);
}


typedef struct flui2d_context_t {
	flui2d_t	fluid;
	float		xf, yf;
} flui2d_context_t;


static void * flui2d_create_context(unsigned ticks, unsigned num_cpus)
{
	flui2d_context_t	*ctxt;

	ctxt = calloc(1, sizeof(flui2d_context_t));
	if (!ctxt)
		return NULL;

	ctxt->fluid.visc = flui2d_viscosity;
	ctxt->fluid.diff = flui2d_diffusion;

	return ctxt;
}


static void flui2d_destroy_context(void *context)
{
	free(context);
}


static int flui2d_fragmenter(void *context, const til_fb_fragment_t *fragment, unsigned number, til_fb_fragment_t *res_fragment)
{
	return til_fb_fragment_tile_single(fragment, 64, number, res_fragment);
}


/* Prepare a frame for concurrent drawing of fragment using multiple fragments */
static void flui2d_prepare_frame(void *context, unsigned ticks, unsigned n_cpus, til_fb_fragment_t *fragment, til_fragmenter_t *res_fragmenter)
{
	flui2d_context_t	*ctxt = context;
	float			r = (ticks % (unsigned)(2 * M_PI * 1000)) * .001f;
	int			x = (cos(r) * .4f + .5f) * (float)ROOT;	/* figure eight pattern for the added densities */
	int			y = (sin(r * 2.f) * .4f + .5f) * (float)ROOT;

	*res_fragmenter = flui2d_fragmenter;

	ctxt->fluid.dens_prev[IX(x, y)] = 1.f;

	/* This orientation for the added velocities at the added densities isn't trying to
	 * emulate any sort of physical relationship to the movement - it's just creating a variety
	 * of turbulence.  It'd be trivial to make it look like a rocket's jetstream or something.
	 */
	ctxt->fluid.u_prev[IX(x, y)] = cos(r * 3.f) * 10.f;
	ctxt->fluid.v_prev[IX(x, y)] = sin(r * 3.f) * 10.f;

	/* These are the core of the simulation, and can't currently be threaded using the paper's implementation, so they
	 * must occur serialized here in prepare_frame.  It would be interesting to try refactor the API and tweak the
	 * implementation for threading, as it would really open up larger field sizes as well as map more naturally to
	 * a GLSL implementation for a fragment shader.
	 */
	vel_step(ROOT, ctxt->fluid.u, ctxt->fluid.v, ctxt->fluid.u_prev, ctxt->fluid.v_prev, ctxt->fluid.visc, .1f);
	dens_step(ROOT, ctxt->fluid.dens, ctxt->fluid.dens_prev, ctxt->fluid.u, ctxt->fluid.v, ctxt->fluid.diff, .1f);

	ctxt->xf = 1.f / fragment->frame_width;
	ctxt->yf = 1.f / fragment->frame_height;
}


/* Draw a the flui2d densities */
static void flui2d_render_fragment(void *context, unsigned ticks, unsigned cpu, til_fb_fragment_t *fragment)
{
	flui2d_context_t	*ctxt = context;

	for (int y = fragment->y; y < fragment->y + fragment->height; y++) {
		int	y0, y1;
		float	Y;

		Y = (float)y * ctxt->yf * (float)ROOT;
		y0 = (int)Y;
		y1 = y0 + 1;

		for (int x = fragment->x; x < fragment->x + fragment->width; x++) {
			float		X, dens, dx0, dx1;
			int		x0, x1;
			uint32_t	pixel;

			X = (float)x * ctxt->xf * (float)ROOT;
			x0 = (int)X;
			x1 = x0 + 1;

			/* linear interpolation of density samples */
			dx0 = ctxt->fluid.dens[(int)IX(x0, y0)] * (1.f - (X - x0));
			dx0 += ctxt->fluid.dens[(int)IX(x1, y0)] * (X - x0);
			dx1 = ctxt->fluid.dens[(int)IX(x0, y1)] * (1.f - (X - x0));
			dx1 += ctxt->fluid.dens[(int)IX(x1, y1)] * (X - x0);
			dens = dx0 * (1.f - (Y - y0)) + dx1 * (Y - y0);

			pixel = ((float)dens * 256.f);
			pixel = pixel << 16 | pixel << 8 | pixel;
			til_fb_fragment_put_pixel_unchecked(fragment, x, y, pixel);
		}
	}
}


/* Settings hooks for configurable variables */
static int flui2d_setup(const til_settings_t *settings, til_setting_desc_t **next_setting)
{
	const char	*viscosity;
	const char	*diffusion;
	const char	*values[] = {
				".000000000001f",
				".0000000001f",
				".000000001f",
				".00000001f",
				".0000001f",
				".000001f",
				".00001f",
				".0001f",
				NULL
			};


	viscosity = til_settings_get_value(settings, "viscosity");
	if (!viscosity) {
		int	r;

		r = til_setting_desc_clone(&(til_setting_desc_t){
						.name = "Fluid Viscosity",
						.key = "viscosity",
						.regex = "\\.[0-9]+",
						.preferred = TIL_SETTINGS_STR(DEFAULT_VISCOSITY),
						.values = values,
						.annotations = NULL
					}, next_setting);
		if (r < 0)
			return r;

		return 1;
	}

	diffusion = til_settings_get_value(settings, "diffusion");
	if (!diffusion) {
		int	r;

		r = til_setting_desc_clone(&(til_setting_desc_t){
						.name = "Fluid Diffusion",
						.key = "diffusion",
						.regex = "\\.[0-9]+",
						.preferred = TIL_SETTINGS_STR(DEFAULT_DIFFUSION),
						.values = values,
						.annotations = NULL
					}, next_setting);
		if (r < 0)
			return r;

		return 1;
	}

	/* TODO: return -EINVAL on parse errors? */
	sscanf(viscosity, "%f", &flui2d_viscosity);
	sscanf(diffusion, "%f", &flui2d_diffusion);

	return 0;
}


til_module_t	flui2d_module = {
	.create_context = flui2d_create_context,
	.destroy_context = flui2d_destroy_context,
	.prepare_frame = flui2d_prepare_frame,
	.render_fragment = flui2d_render_fragment,
	.name = "flui2d",
	.description = "Fluid dynamics simulation in 2D (threaded (poorly))",
	.author = "Vito Caputo <vcaputo@pengaru.com>",
	.license = "Unknown",
	.setup = flui2d_setup,
};
