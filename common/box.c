/*
 * box.c - wrapper code for invoking OpenCL box blur implementation(s).
 *
 * There are three separate implementations of box blur, since the
 * performance of it is so critical to the smooth operation of this program,
 * and different blur radii have very different performance tradeoffs.
 * They are described in detail in box.cl, along with their requirements.
 */
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <strings.h>

#include "common.h"
#include "debug.h"
#include "module.h"
#include "opencl.h"
#include "osdep.h"
#include "randbj.h"
#include "util.h"
#include "box.h"
#include "subblock.h"
#include "boxparams.h"

/* ------------------------------------------------------------------ */

static struct {
	kernel_data_t	manual_box_kernel;
	kernel_data_t	direct_box_kernel;
	kernel_data_t	subblock_box_kernel;
	kernel_data_t	subblock_table_kernel;

	cl_mem		scratch;		/* holding space for 1-D blur */
	cl_mem		subblock_W_params;	/* parameter table */
	cl_mem		subblock_H_params;	/* parameter table, transpose */

	subblock_params_t *debug_params;	/* local copy for debug */
} Box;

/* ------------------------------------------------------------------ */

/*
 * Only have the "debug_params" structure allocated when debugging is enabled.
 */
static void
box_handle_params(void)
{
	/*
	 * This is called just after the debug value is toggled, so the value
	 * of Box.debug_params should reflect what it looked like right before
	 * it was toggled.
	 */
	assert(debug_enabled(DB_BOX) == (Box.debug_params == NULL));

	if (Box.debug_params == NULL) {
		Box.debug_params = mem_alloc(MAX_RADIUS * MAX_NBLOCKS *
		    sizeof (subblock_params_t));
	} else {
		mem_free((void **)&Box.debug_params);
	}
}

static void
box_preinit(void)
{
	debug_register_toggle('b', "box blur", DB_BOX, box_handle_params);
}

static void
box_init_subblock_tables(void)
{
	const size_t		nblocks_size = MAX_NBLOCKS * sizeof (blkidx_t);
	blkidx_t		*nblocks_local;
	cl_mem			nblocks_remote;
	kernel_data_t	*const	kd = &Box.subblock_table_kernel;
	size_t			global[2] = { MAX_NBLOCKS, MAX_RADIUS };
	int			arg;

	nblocks_local = mem_alloc(nblocks_size);
	nblocks_remote = buffer_alloc(nblocks_size);
	for (int radius = 1; radius < MAX_RADIUS; radius++) {
		(void) boxparams_get(radius, &nblocks_local[radius - 1]);
	}
	buffer_writetogpu(nblocks_local, nblocks_remote, nblocks_size);
	mem_free((void **)&nblocks_local);

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Box.subblock_W_params);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &nblocks_remote);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_invoke(kd, 2, global, NULL);

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Box.subblock_H_params);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &nblocks_remote);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_invoke(kd, 2, global, NULL);

	buffer_free(&nblocks_remote);
}

static void
box_init(void)
{
	const size_t	arraysize =
	    (size_t)Width * Height * sizeof (cl_boxvector);
	const size_t	paramsize =
	    MAX_RADIUS * MAX_NBLOCKS * sizeof (subblock_params_t);

	Box.scratch = buffer_alloc(arraysize);
	Box.subblock_W_params = buffer_alloc(paramsize);
	Box.subblock_H_params = buffer_alloc(paramsize);

	kernel_create(&Box.manual_box_kernel,     "manual_box_2d_r1");
	kernel_create(&Box.direct_box_kernel,     "direct_box_1d");
	kernel_create(&Box.subblock_box_kernel,   "subblock_box_1d");
	kernel_create(&Box.subblock_table_kernel, "subblock_build_table");

	// Initialize the boxparams table.
	boxparams_init();

	// Build the subblock parameter tables, now that the boxparams are set.
	box_init_subblock_tables();
}

static void
box_fini(void)
{
	kernel_cleanup(&Box.manual_box_kernel);
	kernel_cleanup(&Box.direct_box_kernel);
	kernel_cleanup(&Box.subblock_box_kernel);
	kernel_cleanup(&Box.subblock_table_kernel);

	buffer_free(&Box.subblock_H_params);
	buffer_free(&Box.subblock_W_params);
	buffer_free(&Box.scratch);
}

const module_ops_t	box_ops = {
	box_preinit,
	box_init,
	box_fini
};

/* ------------------------------------------------------------------ */

static void
invoke_box(kernel_data_t *kd,
    pix_t width, pix_t height, pix_t blockwidth, pix_t blockheight,
    cl_mem src, cl_mem dst, pix_t radius)
{
	size_t	global[2] = {
		P2ROUNDUP(width, blockwidth),
		P2ROUNDUP(height, blockheight)
	};
	size_t	local[2] = { blockwidth, blockheight };
	int	arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &src);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &dst);
	kernel_setarg(kd, arg++,
	    sizeof (cl_boxvector) * local[0] * local[1], NULL);
	kernel_setarg(kd, arg++, sizeof (pix_t), &radius);

	kernel_invoke(kd, 2, global, local);

	if (debug_enabled(DB_BOX)) {
		debug(DB_BOX, "%c r=%3d w=%4u h=%4u "
		    "g=[%4zu %4zu] l=[%4zu %4zu]\n",
		    kd->kd_method[0], radius, width, height,
		    global[0], global[1], local[0], local[1]);
	}
}

static void
invoke_sub(kernel_data_t *kd,
    pix_t width, pix_t height, blkidx_t nblocks, pix_t blockheight,
    cl_mem src, cl_mem dst, pix_t radius, cl_mem params)
{
	size_t	global[2] = { nblocks, P2ROUNDUP(height, blockheight) };
	size_t	local[2] = { nblocks, blockheight };
	int	arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &src);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &dst);
	kernel_setarg(kd, arg++,
	    sizeof (cl_boxvector) * local[0] * local[1], NULL);
	kernel_setarg(kd, arg++, sizeof (pix_t), &radius);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &params);

	kernel_invoke(kd, 2, global, local);

	if (debug_enabled(DB_BOX)) {
		debug(DB_BOX, "s r=%3d wh=[%4u %4u] g=[%4zu %4zu] "
		    "l=[%4zu %4zu] bw=%8.4f",
		    radius, width, height, global[0], global[1],
		    local[0], local[1], (float)width / (float)local[0]);

		buffer_readfromgpu(params, Box.debug_params,
		    (MAX_RADIUS * MAX_NBLOCKS * sizeof (subblock_params_t)));

		/*
		 * Print out the values for blocks 0, 8, 16, 24.
		 */
		for (blkidx_t blk = 0; blk < 32; blk += 8) {
			subblock_params_t	*p =
			    &Box.debug_params[(radius - 1) * MAX_NBLOCKS + blk];
			debug(DB_BOX, " %02hx%02hx%02hx%02hx",
			    p->sp_lblk, p->sp_lpix, p->sp_rblk, p->sp_rpix);
		}
		debug(DB_BOX, "\n");
	}
}

/*
 * This is the actual constraint we need for choosing the maximum block size
 * for box blur; this might be smaller than the device's maximum workgroup size.
 */
static pix_t
box_blur_maxwgsize(box_kernel_t bk)
{
	kernel_data_t	*kd;

	switch (bk) {
	case BK_MANUAL:
		kd = &Box.manual_box_kernel;
		break;

	case BK_DIRECT:
		kd = &Box.direct_box_kernel;
		break;

	case BK_SUBBLOCK:
		kd = &Box.subblock_box_kernel;
		break;

	default:
		assert(0 && "unknown box blur type in box_blur_maxwgsize");
		break;
	}

	return ((pix_t)kernel_wgsize(kd));
}

/*
 * Another API which allows more direct invocation, for testing performance.
 */
static void
box_blur_specific(cl_mem src, cl_mem dst, pix_t radius,
    pix_t width, pix_t height, blkidx_t nblk, box_kernel_t bk, int nbox)
{
	const cl_mem	scratch = Box.scratch;
	kernel_data_t	*kd;

	switch (bk) {
	case BK_MANUAL:
		kd = &Box.manual_box_kernel;
		break;

	case BK_DIRECT:
		kd = &Box.direct_box_kernel;
		break;

	case BK_SUBBLOCK:
		kd = &Box.subblock_box_kernel;
		break;

	default:
		assert(0 && "unknown box blur type in box_blur_specific");
		break;
	}

	/*
	 * The kernel and the width of the workgroup have been chosen;
	 * the height of the workgroup follows directly from that.
	 */
	const pix_t	maxwg = (pix_t)kernel_wgsize(kd);
	const pix_t	h = maxwg / nblk;
	if (nblk * h != maxwg) {
		warn("box_blur_specific: failing assertion: "
		    "nblk (%d) * h (%d) != maxwg (%d)\n", nblk, h, maxwg);
		assert(nblk * h == maxwg);
	}

	switch (bk) {
	case BK_MANUAL:	/* 2-D kernel */
		if (nbox % 2 == 1) {
			invoke_box(kd, width, height, nblk, h,
			    src, dst, radius);
			src = dst;
		}
		for (int i = 0; i < nbox / 2; i++) {
			invoke_box(kd, width, height, nblk, h,
			    src, scratch, radius);
			invoke_box(kd, width, height, nblk, h,
			    scratch, dst, radius);
			src = dst;
		}
		break;

	case BK_DIRECT:
		for (int i = 0; i < nbox; i++) {
			invoke_box(kd, width, height, nblk, h,
			    src, scratch, radius);
			invoke_box(kd, height, width, nblk, h,
			    scratch, dst, radius);
			src = dst;
		}
		break;

	case BK_SUBBLOCK:
		/*
		 * The subblock kernel requires special and non-obvious
		 * configuration of the global[] array. invoke_sub()
		 * takes care of this.
		 */
		for (int i = 0; i < nbox; i++) {
			invoke_sub(kd, width, height, nblk, h,
			    src, scratch, radius, Box.subblock_W_params);
			invoke_sub(kd, height, width, nblk, h,
			    scratch, dst, radius, Box.subblock_H_params);
			src = dst;
		}
		break;

	case BK_NUM_KERNELS:	// don't do this
		assert(0 && "box_blur_specific received BK_NUM_KERNELS");
		break;
	}
}

/*
 * Perform a box blur on the 2-D buffer "src", placing the result in "dst".
 * The radius of the blur is given by "radius".
 */
void
box_blur(cl_mem src, cl_mem dst, pix_t radius, int nbox)
{
	box_kernel_t	bk;
	blkidx_t	nblk;

	bk = boxparams_get(radius, &nblk);

	box_blur_specific(src, dst, radius, Width, Height, nblk, bk, nbox);
}

/* ------------------------------------------------------------------ */

/*
 * Run a box blur performance test on the selected GPU.
 * This is useful for calibrating or updating the heuristics given by the
 * boxparams_init_*() routines in boxparams.c.
 *
 * Unfortunately, this isn't amenable to giving incremental results,
 * since the subblock code needs to be configured with parameters
 * describing how large the block size is before it's actually used.
 */
void
box_test(pix_t min_radius, pix_t max_radius)
{
	const blkidx_t	maxnblk = (blkidx_t)opencl_device_maxwgsize();
	const blkidx_t	minnblk = 4;	// empirically, lower isn't better
	const size_t	lognblk = (size_t)log2((double)(maxnblk / minnblk));
	const size_t	boxsize = Width * Height * sizeof (cl_boxvector);
	cl_boxvector	*localbuf;
	cl_mem		src, dst;
	hrtime_t	**times[BK_NUM_KERNELS];	// [bk][lognblk][rad]
	size_t		i;

	if (max_radius > MAX_RADIUS) {
		die("box_test: max radius must not be larger than %d\n",
		    MAX_RADIUS);
	}

	/*
	 * Create some buffers for doing box blurs, and initialize them
	 * with random (but valid, non-NaN) floating point values.
	 */
	localbuf = mem_alloc(boxsize);
	for (size_t i = 0; i < boxsize / sizeof (float); i++) {
		((float *)localbuf)[i] = (float)drandbj();
	}

	src = buffer_alloc(boxsize);
	dst = buffer_alloc(boxsize);

	buffer_writetogpu(localbuf, src, boxsize);
	buffer_copy(src, dst, boxsize);
	kernel_wait();

	/*
	 * Create the buffers for keeping track of time.
	 */
	for (box_kernel_t bk = 0; bk < BK_NUM_KERNELS; bk++) {
		times[bk] = mem_alloc((lognblk + 1) * sizeof (hrtime_t *));
		for (i = 0; i <= lognblk; i++) {
			const size_t	sz =
			    (max_radius - min_radius + 1) * sizeof (hrtime_t);

			times[bk][i] = mem_alloc(sz);
			for (pix_t radius = min_radius; radius <= max_radius;
			    radius++) {
				times[bk][i][radius - min_radius] = 1e9;
			}
		}
	}

	note("#\n");
	note("# Box blur performance test\n");
	note("# GPU vendor = \"%s\"\n", opencl_device_vendor());
	note("# GPU device = \"%s\"\n", opencl_device_name());
	note("# Buffer size = %zux%zux%d\n", Width, Height, BOX_DIMENSIONS);
	note("#\n");
	note("# rad bk nblk  average -time1- -time2- -time3-\n");

	for (box_kernel_t bk = 0; bk < BK_NUM_KERNELS; bk++) {

		for (size_t i = 0; i <= lognblk; i++) {
			const blkidx_t	nblk = maxnblk >> i;

			if (nblk > box_blur_maxwgsize(bk)) {
				continue;
			}

			/*
			 * Set up the subblock code so it uses this
			 * block size correctly.
			 */
			if (bk == BK_SUBBLOCK) {
				boxparams_init_manual(nblk, bk);
				box_init_subblock_tables();
			}

			for (pix_t radius = min_radius; radius <= max_radius;
			    radius++) {
				hrtime_t	t[4], us;

				// BK_MANUAL only works for r = 1.
				if (bk == BK_MANUAL && radius > 1) {
					break;
				}

				// Do it.
				kernel_wait();
				t[0] = gethrtime();
				for (int i = 0; i < 3; i++) {
					box_blur_specific(src, dst, radius,
					    Width, Height, nblk, bk, 1);
					kernel_wait();
					t[i + 1] = gethrtime();
				}

				// Show the results.
				note("# %3d %2d %4d ", radius, bk, nblk);
				us = (t[3] - t[0]) / 1000 / 3;
				note(" %3llu.%03llu", us / 1000, us % 1000);
				times[bk][i][radius - min_radius] = us;

				for (int i = 0; i < 3; i++) {
					us = (t[i + 1] - t[i]) / 1000;
					note(" %3llu.%03llu",
					    us / 1000, us % 1000);
				}
				note("\n");
			}
		}
	}

	/*
	 * Finally, figure out what's best.
	 */
	note("\n");
	note("# rad bk nblk  average\n");
	for (pix_t radius = min_radius; radius <= max_radius; radius++) {
		box_kernel_t	bestbk;
		blkidx_t	bestnblk;
		hrtime_t	besttime = UINT_MAX;

		for (box_kernel_t bk = 0; bk < BK_NUM_KERNELS; bk++) {

			// BK_MANUAL only works for r = 1.
			if (bk == BK_MANUAL && radius > 1) {
				continue;
			}

			for (size_t i = 0; i <= lognblk; i++) {
				const blkidx_t	nblk = maxnblk >> i;
				const hrtime_t	us =
				    times[bk][i][radius - min_radius];

				if (us < besttime) {
					besttime = us;
					bestbk = bk;
					bestnblk = nblk;
				}
			}
		}

		note("%5d %2d %4d ", radius, bestbk, bestnblk);
		note(" %3llu.%03llu\n", besttime / 1000, besttime % 1000);
	}

	for (box_kernel_t bk = 0; bk < BK_NUM_KERNELS; bk++) {
		for (i = 0; i <= lognblk; i++) {
			mem_free((void **)&times[bk][i]);
		}
		mem_free((void **)&times[bk]);
	}
	mem_free((void **)&localbuf);
	buffer_free(&src);
	buffer_free(&dst);
}
