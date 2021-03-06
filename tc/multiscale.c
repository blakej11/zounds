/*
 * multiscale.c - the Multi-Scale Turing Patterns algorithm.
 *
 * This is based on Jonathan McCabe's algorithm, described at:
 *
 * http://jonathanmccabe.com/Cyclic_Symmetric_Multi-Scale_Turing_Patterns.pdf
 *
 * This implementation doesn't do the "cyclic" part, but it does add color.
 *
 * For each new image, the algorithm generates blurs of the old image using
 * various radii.  It then compares each successive pair of radii, and updates
 * the pixel using a value determined by which pair of blurs had the smallest
 * difference.  (This comparison step is referred to as the "multiscale"
 * algorithm.)
 *
 * To do this in color, this maintains a 4-D data point for each pixel, and
 * it thinks of that data point as a four-dimensional vector.  The blurs also
 * become four-dimensional vectors.  The multiscale algorithm does a vector
 * subtraction on each successive pair of blur data, and it determines the
 * "smallest difference" by looking at the magnitude of the resulting vector.
 * It then adds its update using vector math as well: the update is a vector
 * with a magnitude chosen by which radii had the smallest difference, and a
 * direction chosen by the vector difference between the pair of blurs.
 *
 * The mapping from this 4-D vector onto a colored pixel is given by converting
 * the vector to 4-D polar coordinates, and using the three resulting angles
 * (theta, phi_1, and phi_2) to determine HSV values.  The vector's "radius"
 * is ignored when doing this conversion.
 *
 * The images generated by this algorithm change quite smoothly.  As such,
 * looking at them with heatmap mode is pretty compelling -- it becomes a view
 * of the four-dimensional object that the data points form the surface of.
 *
 * Most of the tweakable bits (including the blur radii and the adjustment
 * values) are in tweak.c.
 *
 * This code is also shared by the "mstp" core algorithm, which implements
 * McCabe's original black-and-white MSTP algorithm.
 */

#include <stdlib.h>

#include "common.h"

#include "box.h"
#include "core.h"
#include "debug.h"
#include "keyboard.h"
#include "module.h"
#include "opencl.h"
#include "osdep.h"
#include "param.h"
#include "tweak.h"

/* ------------------------------------------------------------------ */

#define	NDATA		2	/* number of copies we keep */

static struct {
	core_ops_t	ops;

	cl_mem		data[NDATA];		/* current and last data */
	cl_mem		blurdata[NSCALES];	/* blurred data */
	cl_mem		recentscale;		/* history of which scale */
	int		steps;			/* number of steps taken */

	kernel_data_t	render_kernel;
	kernel_data_t	load_kernel;
	kernel_data_t	unrender_kernel;

	kernel_data_t	multiscale_kernel;
	cl_mem		adj_gpu;		/* adjustment constants */
	float		maxadj;			/* largest adj constant */
} Multiscale;

/* ------------------------------------------------------------------ */

/*
 * The minimum value of any component of a data vector.
 */
float
ms_min(void)
{
	return (-1.0f);
}

/*
 * The maximum value of any component of a data vector.
 */
float
ms_max(void)
{
	return (1.0f);
}

/*
 * Whether datavec's fit into a sphere or a cube.
 */
datavec_shape_t
ms_datavec_shape(void)
{
	return (DATAVEC_SHAPE_SPHERE);
}

/* ------------------------------------------------------------------ */

static void
ms_unrender(cl_mem image, cl_mem data)
{
	kernel_data_t	*const	kd = &Multiscale.unrender_kernel;
	int			arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &image);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &data);
	kernel_wait();
	kernel_invoke(kd, 2, NULL, NULL);
}

static void
ms_import(cl_mem src)
{
	kernel_data_t	*const	kd = &Multiscale.load_kernel;
	cl_mem			dst = Multiscale.data[0];
	int			arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &src);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &dst);
	kernel_invoke(kd, 2, NULL, NULL);

	const size_t		arraysize =
	    (size_t)Width * Height * sizeof (cl_datavec);
	buffer_copy(Multiscale.data[0], Multiscale.data[1], arraysize);
}

static void
ms_combine_and_export(
	cl_mem *densities,
	cl_mem odata,
	cl_mem ndata,
	int nscales,
	cl_mem result)
{
	kernel_data_t	*const	kd = &Multiscale.multiscale_kernel;
	int			arg, i;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	for (i = 0; i < NSCALES; i++, arg++) {
		kernel_setarg(kd, arg, sizeof (cl_mem), &densities[i]);
	}
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Multiscale.adj_gpu);
	kernel_setarg(kd, arg++, sizeof (float), &Multiscale.maxadj);
	kernel_setarg(kd, arg++, sizeof (int), &nscales);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &odata);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &ndata);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Multiscale.recentscale);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &result);
	kernel_invoke(kd, 2, NULL, NULL);
}

static void
ms_render(cl_mem data, cl_mem image)
{
	kernel_data_t	*const	kd = &Multiscale.render_kernel;
	int			rendertype = tweak_rendertype();
	int			arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (int), &rendertype);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &data);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Multiscale.recentscale);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &image);
	kernel_invoke(kd, 2, NULL, NULL);
}

/* ------------------------------------------------------------------ */

/*
 * A callback that gets invoked when the "adjtype" or "speed" parameters
 * change. This adjusts the constants used by the OpenCL kernels.
 */
void
multiscale_adjust(void)
{
	const int	ndiff = NSCALES - 1;
	float		adj[NSCALES - 1];

	Multiscale.maxadj = 0;
	for (int i = 0; i < ndiff; i++) {
		adj[i] = tweak_multiscale_adj(i);
		Multiscale.maxadj = MAX(adj[i], Multiscale.maxadj);
	}
	debug(DB_CORE, "Setting adj weights: [");
	for (int i = 0; i < ndiff; i++) {
		debug(DB_CORE, " %.3f", adj[i]);
	}
	debug(DB_CORE, " ]\n");
	buffer_writetogpu(adj, Multiscale.adj_gpu, ndiff * sizeof (float));
}

/*
 * The Multi-Scale Turing Patterns algorithm.
 */
static void
ms_step(cl_mem result)
{
	const int	nscales = tweak_nscales();
	const int	nbox = tweak_nbox();
	cl_mem		src = Multiscale.data[(Multiscale.steps & 1)];
	cl_mem		dst = Multiscale.data[!(Multiscale.steps & 1)];

	Multiscale.steps++;

	/*
	 * If we're not measuring performance, don't add in any extra
	 * calls to kernel_wait().
	 */
	if (!debug_enabled(DB_PERF)) {
		/*
		 * Do a box blur at each scale.  Changing the number of
		 * box blur passes yields visually interesting results.
		 */
		for (int sc = 0; sc < nscales; sc++) {
			const pix_t	radius = tweak_box_radius(sc);

			box_blur(src, Multiscale.blurdata[sc], radius, nbox);
		}

		/*
		 * Use the box blurs to determine how to update each pixel.
		 */
		ms_combine_and_export(Multiscale.blurdata,
		    src, dst, nscales, result);
	} else {
		hrtime_t	t[3];
		t[0] = gethrtime();

		for (int sc = 0; sc < nscales; sc++) {
			const pix_t	radius = tweak_box_radius(sc);
			cl_mem		bsrc, bdst;
			hrtime_t	u;

			u = gethrtime();
			box_blur(src, Multiscale.blurdata[sc], radius, nbox);
			kernel_wait();

			u = gethrtime() - u;
			debug(DB_PERF, "%5.2lf ", (double)u / 1000000.0);
		}

		t[1] = gethrtime();

		ms_combine_and_export(Multiscale.blurdata,
		    src, dst, nscales, result);
		kernel_wait();

		t[2] = gethrtime();

		debug(DB_PERF, " | %5.2lf | %7.2lf",
		    (double)(t[2] - t[1]) / 1000000.0,
		    (double)(t[2] - t[0]) / 1000000.0);
	}
}

/* ------------------------------------------------------------------ */

static void
ms_preinit(void)
{
	debug_register_toggle('c', "core algorithm", DB_CORE, NULL);
	debug_register_toggle('P', "performance", DB_PERF, NULL);

	Multiscale.ops.unrender = ms_unrender;
	Multiscale.ops.import = ms_import;
	Multiscale.ops.step_and_export = ms_step;
	Multiscale.ops.render = ms_render;
	Multiscale.ops.min = ms_min;
	Multiscale.ops.max = ms_max;
	Multiscale.ops.datavec_shape = ms_datavec_shape;

	tweak_preinit();
}

static void
ms_init(void)
{
	const size_t	boxsize =
	    (size_t)Width * Height * sizeof (cl_boxvector);
	const size_t	datasize =
	    (size_t)Width * Height * sizeof (cl_datavec);
	const size_t	scalesize =
	    (size_t)Width * Height * sizeof (float);

	core_ops_register(&Multiscale.ops);

	for (int sc = 0; sc < NSCALES; sc++) {
		Multiscale.blurdata[sc] = buffer_alloc(boxsize);
	}
	for (int nd = 0; nd < NDATA; nd++) {
		Multiscale.data[nd] = buffer_alloc(datasize);
	}
	Multiscale.recentscale = buffer_alloc(scalesize);

	kernel_create(&Multiscale.unrender_kernel, "unrender");
	kernel_create(&Multiscale.load_kernel, "import");
	kernel_create(&Multiscale.multiscale_kernel, "multiscale");
	kernel_create(&Multiscale.render_kernel, "render");

	Multiscale.adj_gpu = buffer_alloc(NSCALES * sizeof (float));

	tweak_init();
}

static void
ms_fini(void)
{
	tweak_fini();

	kernel_cleanup(&Multiscale.multiscale_kernel);
	buffer_free(&Multiscale.adj_gpu);

	kernel_cleanup(&Multiscale.render_kernel);
	kernel_cleanup(&Multiscale.load_kernel);
	kernel_cleanup(&Multiscale.unrender_kernel);

	buffer_free(&Multiscale.recentscale);
	for (int nd = 0; nd < NDATA; nd++) {
		buffer_free(&Multiscale.data[nd]);
	}
	for (int sc = 0; sc < NSCALES; sc++) {
		buffer_free(&Multiscale.blurdata[sc]);
	}

	core_ops_unregister(&Multiscale.ops);
}

const module_ops_t	core_ops = {
	ms_preinit,
	ms_init,
	ms_fini
};
