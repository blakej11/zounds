/*
 * reduce.c - reduces an image down to a much smaller version of itself.
 *
 * This uses OpenCL to perform the image reduction.  This isn't a fantastic
 * fit for OpenCL, since it involves coordinating results across many pixels,
 * but it seems to perform better (on my laptop) than doing it on the CPU.
 * The final step of the reduction kernel is to add the intermediate result
 * to a global tracking array, using atomic_add().  But OpenCL can only use
 * atomic_add() on 32-bit integral data types.  So, rather than having a
 * nicely general image reduction kernel, the last step picks out one
 * component of the vector, scales it up by 256, converts it to an integer,
 * and adds that to the tracking array.
 */
#include <strings.h>
#include <assert.h>

#include "common.h"

#include "debug.h"
#include "interp.h"
#include "module.h"
#include "opencl.h"
#include "param.h"
#include "reduce.h"
#include "util.h"

/* ------------------------------------------------------------------ */

static struct {
	kernel_data_t	reduce_kernel;	/* reduction kernel */

	cl_mem		reduce_gpu;	/* memory for reduction */
	size_t		reduce_bufsize;
} Reduce;

/* ------------------------------------------------------------------ */

static void
reduce_init(void)
{
	kernel_create(&Reduce.reduce_kernel, "reduce");

	Reduce.reduce_bufsize = 0;
}

static void
reduce_fini(void)
{
	if (Reduce.reduce_bufsize > 0) {
		buffer_free(&Reduce.reduce_gpu);
	}

	kernel_cleanup(&Reduce.reduce_kernel);
}

const module_ops_t	reduce_ops = {
	NULL,
	reduce_init,
	reduce_fini
};

/* ------------------------------------------------------------------ */

/*
 * Wrapper around the OpenCL kernel to do image reduction.
 *
 * - "data" is an image2d_t made of datavec's.
 * - "dim" is which dimension of the datavec to do the reduction upon.
 *   (Right now it only does one at a time.)
 * - "min" and "max" are the bounds of possible data values in the datavec.
 *
 * - "tgtbuffer" is a buffer of "bufedge" * "bufedge" int's,
 *   which will hold the reduced result.
 *
 * The reduce kernel actually just adds up the pixel values and scales
 * them so they are within a [0, 256) range per pixel.
 */
void
reduce_addup(cl_mem data, int dim, float min, float max,
    int *tgtbuffer, pix_t bufedge)
{
	const size_t		bufsize =
	    (size_t)bufedge * bufedge * sizeof (int);
	kernel_data_t	*const	kd = &Reduce.reduce_kernel;
	int			arg;

	if (Reduce.reduce_bufsize != bufsize) {
		if (Reduce.reduce_bufsize > 0) {
			buffer_free(&Reduce.reduce_gpu);
		}
		Reduce.reduce_gpu = buffer_alloc(bufsize);
		Reduce.reduce_bufsize = bufsize;
	}

	bzero(tgtbuffer, bufsize);
	buffer_writetogpu(tgtbuffer, Reduce.reduce_gpu, bufsize);

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &data);
	kernel_setarg(kd, arg++, bufedge * bufedge * sizeof (cl_datavec), NULL);
	kernel_setarg(kd, arg++, bufedge * bufedge * sizeof (int), NULL);
	kernel_setarg(kd, arg++, sizeof (pix_t), &bufedge);
	kernel_setarg(kd, arg++, sizeof (int), &dim);
	kernel_setarg(kd, arg++, sizeof (float), &min);
	kernel_setarg(kd, arg++, sizeof (float), &max);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Reduce.reduce_gpu);
	kernel_invoke(kd, 2, NULL, NULL);

	buffer_readfromgpu(Reduce.reduce_gpu, tgtbuffer, bufsize);
}

/*
 * The reduce kernel just adds up the results; here we scale them down to get
 * an average value for each resulting pixel.  The extra rigmarole for
 * calculating "dx" and "dy" is because bufedge might not divide Width or
 * Height evenly, and we want to get the same pixel count as the OpenCL kernel
 * uses.
 */
static void
reduce_normalize(int *tgtbuffer, pix_t bufedge)
{
	int		x, y, p;

	for (p = y = 0; y < bufedge; y++) {
		for (x = 0; x < bufedge; x++, p++) {
			const int	dx = ((x + 1) * Width / bufedge) -
			    (x * Width / bufedge);
			const int	dy = ((y + 1) * Height / bufedge) -
			    (y * Height / bufedge);

			tgtbuffer[p] /= dx * dy;
		}
	}
}

void
reduce(cl_mem data, int dim, float min, float max,
    int *tgtbuffer, pix_t bufedge)
{
	reduce_addup(data, dim, min, max, tgtbuffer, bufedge);
	reduce_normalize(tgtbuffer, bufedge);
}
