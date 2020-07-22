/*
 * camdelta.c - routines for taking the difference of two camera frames.
 */

#include <stdlib.h>
#include <strings.h>
#include <math.h>
#include <assert.h>

#include "common.h"

#include "camera.h"
#include "debug.h"
#include "module.h"
#include "opencl.h"
#include "osdep.h"
#include "reduce.h"
#include "util.h"

/* ------------------------------------------------------------------ */

#define	NDATA		2	/* number of copies we keep */

static struct {
	bool		disabled;		/* no camera -> can't use */

	cl_mem		camera[NDATA];		/* uchar3's, packed BGR data */
	pix_t		camwidth, camheight;	/* size of camera image */
	size_t		camsize;

	// for doing image reduction
	int		*reduced_cpu;
	int		reduced_bufedge;
	float		delta_i;		/* average intensity diff */
	float		rolling_delta_i;	/* DMA of delta_i */

	int		steps;			/* number of steps taken */

	kernel_data_t	delta_kernel;		/* the comparison kernel */
} Camdelta;

/* ------------------------------------------------------------------ */

/*
 * We use the amount of motion in a given camera frame to control how many
 * steps to take. But the raw amount of motion varies from camera to camera
 * and scene to scene, so we want to compare the current motion to the recent
 * history of motion.
 */
float
camdelta_intensity(void)
{
	float	res;

	if (Camdelta.disabled) {
		return (1.0);		/* make this a no-op */
	} else {
		assert(Camdelta.rolling_delta_i != 0.0);
		res = Camdelta.delta_i / Camdelta.rolling_delta_i;

		/*
		 * Make sure we're not moving too wildly.
		 */
		res = MIN(MAX(res, 1.0), 10.0);

		/*
		 * This makes it easy for just a little bit of movement to
		 * tip #steps from 1 to 2.
		 */
		res *= 1.8;

		debug(DB_CAMERA, "Intensity: %5.2f\n", res);
		return (res);
	}
}

static void
calc_delta_intensities(cl_mem curdelta)
{
	const int		bufedge = Camdelta.reduced_bufedge;
	int		*const	reduced = Camdelta.reduced_cpu;
	int64_t			sum;
	const float		ema_factor = 0.005;

	reduce_addup(curdelta, 3, 0.0, 1.0, reduced, bufedge);
	sum = 0;
	for (int i = 0; i < bufedge * bufedge; i++) {
		sum += (int64_t)reduced[i];
	}

	Camdelta.delta_i = ((float)sum / (float)(Width * Height));
	Camdelta.rolling_delta_i =
	    Camdelta.rolling_delta_i * (1.0 - ema_factor) +
	    Camdelta.delta_i * ema_factor;
}

/*
 * Grab a frame from the camera, figure out what parts of the image changed
 * compared to the last frame, and figure out how much overall motion there was.
 */
void
camdelta_step(cl_mem newdelta)
{
	kernel_data_t	*const	kd = &Camdelta.delta_kernel;
	const int		steps = Camdelta.steps;
	const size_t		size = Camdelta.camsize;
	cl_mem			ocap = Camdelta.camera[(steps + 1) % 2];
	cl_mem			ncap = Camdelta.camera[(steps + 0) % 2];
	bool			grabbed;
	uint8_t			*bgr;
	int			arg;
	hrtime_t		t[6];

	if (Camdelta.disabled) {
		return;
	}

	Camdelta.steps++;

	/*
	 * Grab and retrieve a frame from the camera.
	 */
	t[0] = gethrtime();
	grabbed = camera_grab();
	t[1] = gethrtime();
	bgr = camera_retrieve();
	t[2] = gethrtime();
	if (grabbed == false || bgr == NULL) {
		if (grabbed == false) {
			warn("camdelta_step(): failed to grab an image\n");
		} else {
			warn("camdelta_step(): failed to retrieve an image\n");
		}

		return;
	}

	/*
	 * Copy the frame to the GPU.
	 */
	buffer_writetogpu(bgr, ncap, size);
	if (debug_enabled(DB_PERF)) {
		kernel_wait();
	}
	t[3] = gethrtime();

	/*
	 * Run the difference kernel.
	 */
	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Camdelta.camwidth);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Camdelta.camheight);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &ocap);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &ncap);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &newdelta);
	kernel_invoke(kd, 2, NULL, NULL);

	if (debug_enabled(DB_PERF)) {
		kernel_wait();
	}
	t[4] = gethrtime();

	/*
	 * Figure out how intense that delta was.
	 */
	calc_delta_intensities(newdelta);
	t[5] = gethrtime();

	debug(DB_CAMERA, "Intensities: %f\n", Camdelta.delta_i);

	debug(DB_PERF, "C:    %5.2lf %5.2lf %5.2lf %5.2lf %5.2lf | %7.2lf\n",
	    (double)(t[1] - t[0]) / 1000000.0,
	    (double)(t[2] - t[1]) / 1000000.0,
	    (double)(t[3] - t[2]) / 1000000.0,
	    (double)(t[4] - t[3]) / 1000000.0,
	    (double)(t[5] - t[4]) / 1000000.0,
	    (double)(t[5] - t[0]) / 1000000.0);
}

/* ------------------------------------------------------------------ */

static void
camdelta_preinit(void)
{
	/*
	 * Do this in preinit(), so other init() operations can take actions
	 * that depend on whether the camera is in use.
	 */
	if (camera_disabled() || !camera_init()) {
		Camdelta.disabled = true;
	} else {
		Camdelta.disabled = false;
	}
}

static void
camdelta_init(void)
{
	if (!Camdelta.disabled) {
		const size_t	camwidth = camera_width();
		const size_t	camheight = camera_height();
		const size_t	camsize =
		    camwidth * camheight * 3 * sizeof (char);

		Camdelta.camwidth = camwidth;
		Camdelta.camheight = camheight;
		Camdelta.camsize = camsize;
		for (int nd = 0; nd < NDATA; nd++) {
			Camdelta.camera[nd] = buffer_alloc(camsize);
		}

		Camdelta.reduced_bufedge = 16;	/* somewhat arbitrary */
		Camdelta.reduced_cpu = mem_alloc(Camdelta.reduced_bufedge *
		    Camdelta.reduced_bufedge * sizeof (int));
		Camdelta.rolling_delta_i = 1.0;

		kernel_create(&Camdelta.delta_kernel, "camera_delta");
	} else {
		Camdelta.camwidth = 0;
		Camdelta.camheight = 0;
		for (int nd = 0; nd < NDATA; nd++) {
			Camdelta.camera[nd] = NULL;
		}

		Camdelta.reduced_bufedge = 0;
		Camdelta.reduced_cpu = NULL;
	}
}

static void
camdelta_fini(void)
{
	if (!Camdelta.disabled) {
		kernel_cleanup(&Camdelta.delta_kernel);

		for (int nd = 0; nd < NDATA; nd++) {
			buffer_free(&Camdelta.camera[nd]);
		}
		Camdelta.camwidth = Camdelta.camheight = 0;

		Camdelta.reduced_bufedge = 0;
		mem_free((void **)&Camdelta.reduced_cpu);
	}
}

static void
camdelta_postfini(void)
{
	if (!Camdelta.disabled) {
		camera_fini();
	}
}

const module_ops_t	camdelta_ops = {
	camdelta_preinit,
	camdelta_init,
	camdelta_fini,
	camdelta_postfini
};
