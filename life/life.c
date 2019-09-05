/*
 * life.c - a core algorithm implementing John Conway's Game of Life.
 *
 * This has one tunable parameter: the "aliveness" threshold.  Each pixel has
 * a value between 0 and 1, and any pixels whose value is at or above the
 * aliveness threshold are considered alive from the perspective of the
 * Game of Life algorithm.  Pixels that are alive for multiple timesteps in
 * a row decrease their value linearly down to the threshold, and the color
 * of a pixel is set by how alive it is. (Freshly generated alive pixels are
 * red, while pixels that are just at the aliveness threshold are dark blue.)
 * The default threshold value is 0.75.
 *
 * Strokes (from ../stroke.c) only do anything useful at a threshold of about
 * 0.1, because they are blurring in so many non-alive pixels that they tend
 * to drop most pixels below the threshold.  Gently lowering the threshold
 * down to 0.1, and using a stroke width of 1, gives something slightly
 * interesting.
 */

#include <stdlib.h>
#include <strings.h>

#include "common.h"

#include "box.h"
#include "core.h"
#include "debug.h"
#include "keyboard.h"
#include "module.h"
#include "opencl.h"
#include "osdep.h"
#include "param.h"
#include "randbj.h"
#include "shared.h"
#include "util.h"

/* ------------------------------------------------------------------ */

struct {
	core_ops_t	ops;

	kernel_data_t	unrender_kernel;
	kernel_data_t	import_kernel;
	kernel_data_t	step_kernel;
	kernel_data_t	render_kernel;

	param_id_t	threshid;

	cl_mem		arena[2];	/* Width * Height * sizeof (float) */
	cl_mem		random;		/* state for RNG */
	int		steps;
} Life;

/* ------------------------------------------------------------------ */

/*
 * The minimum value of any component of a data vector.
 */
float
life_min(void)
{
	return (0.0f);
}

/*
 * The maximum value of any component of a data vector.
 */
float
life_max(void)
{
	return (1.0f);
}

/*
 * Whether datavec's fit into a sphere or a cube.
 * (here it doesn't matter, since they're 1-D)
 */
datavec_shape_t
life_datavec_shape(void)
{
	return (DATAVEC_SHAPE_SPHERE);
}

/* ------------------------------------------------------------------ */

static float
life_threshold(void)
{
	return (param_float(Life.threshid));
}

static void
life_unrender(cl_mem image, cl_mem data)
{
	kernel_data_t	*const	kd = &Life.unrender_kernel;
	float			thresh = life_threshold();
	int			arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (float), &thresh);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &image);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &data);
	kernel_invoke(kd, 2, NULL, NULL);
}

static void
life_import(cl_mem data)
{
	kernel_data_t	*const	kd = &Life.import_kernel;
	float			thresh = life_threshold();
	const int		cur = (Life.steps & 1);
	int			arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (float), &thresh);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &data);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Life.arena[cur]);
	kernel_invoke(kd, 2, NULL, NULL);
}

static void
life_step(cl_mem result)
{
	kernel_data_t	*const	kd = &Life.step_kernel;
	float			thresh = life_threshold();
	int			steps = Life.steps;
	const int		cur = (steps & 1);
	int			arg;

	Life.steps++;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (float), &thresh);
	kernel_setarg(kd, arg++, sizeof (int), &steps);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Life.random);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Life.arena[cur]);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Life.arena[cur ^ 1]);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &result);
	kernel_invoke(kd, 2, NULL, NULL);

	/*
	 * Debugging: display the values of the pixels in the immediate
	 * neighborhood of debug_offset().
	 */
	if (debug_enabled(DB_CORE)) {
		const pix_t	off = debug_offset();
		const spix_t	X = off / Width;
		const spix_t	Y = off % Width;
		spix_t		x, y;

		debug(DB_CORE, "%d %5d: ||", off, steps);

#define	WRAP(x,max)	(((x) + (max)) % (max))
#define	PIXEL(x,y,w,h)	(((WRAP(y,h)) * (w)) + (WRAP(x,w)))

		for (y = Y - 1; y <= Y + 1; y++) {
			for (x = X - 1; x <= X + 1; x++) {
				pix_t	p = PIXEL(x, y, Width, Height);
				debug(DB_CORE, " %7.4f",
				    buffer_float_at(Life.arena[cur], p));
			}
			debug(DB_CORE, " |");
		}

#undef	WRAP
#undef	PIXEL

		debug(DB_CORE, "| -> %7.4f\n",
		    buffer_float_dbg(Life.arena[cur ^ 1]));
	}
}

static void
life_render(cl_mem data, cl_mem image)
{
	kernel_data_t	*const	kd = &Life.render_kernel;
	float			thresh = life_threshold();
	int			arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (float), &thresh);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &data);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &image);
	kernel_invoke(kd, 2, NULL, NULL);
}

/* ------------------------------------------------------------------ */

static void
life_preinit(void)
{
	param_init_t	pi;

	bzero(&pi, sizeof (pi));

	/*
	 * Aliveness threshold.
	 */
	pi.pi_min = THRESH_SCALE * 0.01f;
	pi.pi_default = THRESH_SCALE * 0.75f;
	pi.pi_max = THRESH_SCALE;
	pi.pi_units = 1.0f / (float)THRESH_SCALE;
	pi.pi_ap_freq = AP_FREQ_OFF;
	pi.pi_ap_rate = AP_RATE_OFF;
	Life.threshid = param_register("aliveness", &pi);

	param_key_register('-', KB_DEFAULT, Life.threshid, -1);
	param_key_register('_', KB_DEFAULT, Life.threshid, -1);
	param_key_register('+', KB_DEFAULT, Life.threshid,  1);
	param_key_register('=', KB_DEFAULT, Life.threshid,  1);

	Life.ops.unrender = life_unrender;
	Life.ops.import = life_import;
	Life.ops.step_and_export = life_step;
	Life.ops.render = life_render;
	Life.ops.min = life_min;
	Life.ops.max = life_max;
	Life.ops.datavec_shape = life_datavec_shape;

	debug_register_toggle('c', "core algorithm", DB_CORE, NULL);
}

static void
life_init(void)
{
	const size_t	arenasize = (size_t)Width * Height * sizeof (float);
	const size_t	randsize = (size_t)Width * Height * sizeof (cl_uint4);
	cl_uint4	*rand_cpu;

	core_ops_register(&Life.ops);

	for (int i = 0; i < sizeof (Life.arena) / sizeof (*Life.arena); i++) {
		Life.arena[i] = buffer_alloc(arenasize);
	}

	/* Initialize state for the PRNG. */
	Life.random = buffer_alloc(randsize);
	rand_cpu = mem_alloc(randsize);
	for (pix_t i = 0; i < Width * Height; i++) {
		rand_cpu[i].x = (cl_uint)lrandbj();
		rand_cpu[i].y = (cl_uint)lrandbj();
		rand_cpu[i].z = (cl_uint)lrandbj();
		rand_cpu[i].w = (cl_uint)lrandbj();
	}
	buffer_writetogpu(rand_cpu, Life.random, randsize);
	mem_free((void **)&rand_cpu);

	kernel_create(&Life.unrender_kernel, "unrender");
	kernel_create(&Life.import_kernel, "import");
	kernel_create(&Life.step_kernel, "step_and_export");
	kernel_create(&Life.render_kernel, "render");

	Life.steps = 0;
}

static void
life_fini(void)
{
	kernel_cleanup(&Life.render_kernel);
	kernel_cleanup(&Life.step_kernel);
	kernel_cleanup(&Life.import_kernel);
	kernel_cleanup(&Life.unrender_kernel);

	for (int i = 0; i < sizeof (Life.arena) / sizeof (*Life.arena); i++) {
		buffer_free(&Life.arena[i]);
	}
	buffer_free(&Life.random);

	core_ops_unregister(&Life.ops);
}

const module_ops_t	core_ops = {
	life_preinit,
	life_init,
	life_fini
};
