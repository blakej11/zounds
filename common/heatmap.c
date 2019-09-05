/*
 * heatmap.c - wrapper code for invoking the OpenCL heatmap implementation.
 *
 * The core algorithm generates an "image" that's really a set of
 * multi-dimensional data points.  It has a render() routine to turn those
 * into a pretty picture, but it can be useful to visualize the data points
 * more directly.  This code creates a heatmap of the data by projecting
 * all of the data points onto two dimensions, and coloring the pixels
 * based on how many data points map to that pixel in the projection.
 * "Hotter" colors indicate more pixels map onto it.
 */

#include <math.h>
#include <strings.h>

#include "basis.h"
#include "common.h"
#include "debug.h"
#include "opencl.h"
#include "keyboard.h"
#include "heatmap.h"
#include "module.h"
#include "param.h"
#include "randbj.h"
#include "window.h"

/*
 * In picture-in-picture mode, the heatmap gets to take up one sixteenth
 * (0.25 * 0.25) of the overall image. It is placed in one of the corners.
 */
#define	PIP_FRACTION	0.25

/* ------------------------------------------------------------------ */

#if	DATA_DIMENSIONS == 1

/*
 * Heatmaps just don't work with one-dimensional data.  So here are some
 * stub symbols to allow this to compile in 1-D.
 */
const module_ops_t	heatmap_ops = { };

void
heatmap_update(cl_mem data, float min, float max, datavec_shape_t shape,
    cl_mem image)
{
}

#else	/* DATA_DIMENSIONS > 1 */

static void	heatmap_toggle(void);

/* ------------------------------------------------------------------ */

static struct {
	kernel_data_t		zero_kernel;
	kernel_data_t		histogram_kernel;
	kernel_data_t		render_kernel;

	cl_mem			histogram;

	enum { OFF, PIP, ON }	state;		/* PIP = picture-in-picture */
} Heatmap;

/* ------------------------------------------------------------------ */

static void
heatmap_preinit(void)
{
	int		i;

	Heatmap.state = OFF;

	key_register('h', KB_DEFAULT, "toggle heatmap mode", heatmap_toggle);
	key_register('=', KB_KEYPAD, "toggle heatmap mode", heatmap_toggle);
	debug_register_toggle('h', "heatmap", DB_HEAT, NULL);
}

static void
heatmap_init(void)
{
	if (Heatmap.state != OFF) {
		const size_t	mapsize = (size_t)Width * Height * sizeof (int);

		kernel_create(&Heatmap.zero_kernel, "hm_zero");
		kernel_create(&Heatmap.histogram_kernel, "hm_histogram");
		kernel_create(&Heatmap.render_kernel, "hm_render");

		Heatmap.histogram = buffer_alloc(mapsize);
	}
}

static void
heatmap_fini(void)
{
	if (Heatmap.state != OFF) {
		buffer_free(&Heatmap.histogram);

		kernel_cleanup(&Heatmap.render_kernel);
		kernel_cleanup(&Heatmap.histogram_kernel);
		kernel_cleanup(&Heatmap.zero_kernel);
	}
}

const module_ops_t	heatmap_ops = {
	heatmap_preinit,
	heatmap_init,
	heatmap_fini
};

/* ------------------------------------------------------------------ */

static void
heatmap_toggle(void)
{
	switch (Heatmap.state) {
	case OFF:
		Heatmap.state = PIP;
		heatmap_init();
		break;
	case PIP:
		Heatmap.state = ON;
		break;
	case ON:
		heatmap_fini();
		Heatmap.state = OFF;
		break;
	}

	verbose(0, "Heat map %sabled%s\n",
	    (Heatmap.state == OFF) ? "dis" : "en",
	    (Heatmap.state == PIP) ? " (picture-in-picture)" : "");

	window_update();
}

/*
 * Update "image" with a heatmap image, based on the data that was accumulated
 * in "data".
 */
void
heatmap_update(cl_mem data, float min, float max, datavec_shape_t shape,
    cl_mem image)
{
	kernel_data_t	*kd;
	int		arg;
	float		hmscale;
	cl_datavec	bases[2];
	float		scale;

	/*
	 * Pivot the projection, and create new basis vectors.
	 *
	 * We do this even if we're not using the heatmap mode at the moment.
	 * That ensures that we use the same quantity of pseudorandom numbers
	 * whether we're using the heatmap or not, so we can observe a system
	 * that was initialized with a given random seed without disturbing it.
	 */
	basis_update(bases);

	switch (Heatmap.state) {
	case OFF:
		return;
	case PIP:
		hmscale = PIP_FRACTION;
		break;
	case ON:
		hmscale = 1.0;
		break;
	}

	/*
	 * Zero out the heatmap histogram, so we can increment it.
	 */
	kd = &Heatmap.zero_kernel;
	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Heatmap.histogram);
	kernel_invoke(kd, 2, NULL, NULL);

	/*
	 * Data which is cube-like needs to be shrunk down so it fits into
	 * a unit sphere, since this may be looking at it from any basis.
	 */
	if (shape == DATAVEC_SHAPE_CUBE) {
		scale = 1.0f / sqrtf((float)DATA_DIMENSIONS);
	} else {
		scale = 1.0f;
	}

	/*
	 * Generate the heatmap histogram from the data.
	 */
	kd = &Heatmap.histogram_kernel;
	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (float), &min);
	kernel_setarg(kd, arg++, sizeof (float), &max);
	kernel_setarg(kd, arg++, sizeof (float), &scale);
	kernel_setarg(kd, arg++, sizeof (cl_datavec), &bases[0]);
	kernel_setarg(kd, arg++, sizeof (cl_datavec), &bases[1]);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &data);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Heatmap.histogram);
	kernel_invoke(kd, 2, NULL, NULL);

	/*
	 * Render the histogram into a colored image.
	 */
	kd = &Heatmap.render_kernel;
	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Heatmap.histogram);
	kernel_setarg(kd, arg++, sizeof (float), &hmscale);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &image);
	kernel_invoke(kd, 2, NULL, NULL);
}

#endif	/* DATA_DIMENSIONS > 1 */
