/*
 * The choice of kernel and work group size is experimentally determined,
 * based on what performs well on a couple systems.  The choice shouldn't
 * matter for correctness, just speed.
 *
 * These values are all chosen for box blurs of 4-D data
 * (sizeof (cl_boxvector) == 4 * sizeof (float)).
 * There may be better values for 1-D data.
 *
 * Obviously it would be much better to have this auto-tuned.
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "common.h"
#include "debug.h"
#include "opencl.h"
#include "subblock.h"	/* for MAX_NBLOCKS */

/* ------------------------------------------------------------------ */

typedef struct {
	blkidx_t	nblk;	// number of blocks per line
	box_kernel_t	bk;	// which box kernel to use
} box_params_t;

static struct {
	box_params_t	params[MAX_RADIUS];
} Bp;

static void
boxparams_set(pix_t radius, blkidx_t nblk, box_kernel_t bk)
{
	assert(radius > 0 && radius <= MAX_RADIUS);
	Bp.params[radius - 1].nblk = nblk;
	Bp.params[radius - 1].bk = bk;
}

box_kernel_t
boxparams_get(pix_t radius, blkidx_t *nblkp)
{
	assert(radius > 0 && radius <= MAX_RADIUS);
	*nblkp = Bp.params[radius - 1].nblk;
	return (Bp.params[radius - 1].bk);
}

/* ------------------------------------------------------------------ */

/*
 * tuned for: "Intel(R) Iris(TM) Graphics 550", 1920x1080x4
 *	(64K local mem, 48 compute units)
 *
 * also seen: "Intel(R) UHD Graphics 630"
 *	(64K local mem, 24 compute units)
 */
static void
boxparams_init_intel(const char *device,
    pix_t radius, blkidx_t *nblkp, box_kernel_t *bkp)
{
	if (radius == 1) {
		*nblkp = 32;
		*bkp = BK_MANUAL;
	} else if (radius <= 7) {
		*nblkp = 32;
		*bkp = BK_DIRECT;
	} else if (radius <= 87) {
		*nblkp = 256;
		*bkp = BK_SUBBLOCK;
	} else {
		*nblkp = 128;
		*bkp = BK_SUBBLOCK;
	}
}

/*
 * tuned for: "AMD Radeon RX 570 Compute Engine", 1920x1080x4
 *	(32K local mem, 32 compute units)
 */
static void
boxparams_init_amd(const char *device,
    pix_t radius, blkidx_t *nblkp, box_kernel_t *bkp)
{
	if (radius == 1) {
		*nblkp = 256;
		*bkp = BK_MANUAL;
	} else if (radius <= 14) {
		*nblkp = 16;
		*bkp = BK_DIRECT;
	} else if (radius <= 18) {
		*nblkp = 32;
		*bkp = BK_DIRECT;
	} else {
		*nblkp = 4;
		*bkp = BK_SUBBLOCK;
	}
}

/*
 * tuned for device == "GeForce GTX 1060 6GB", 1920x1080x4
 *	(48K local mem, 10 compute units)
 */
static void
boxparams_init_nvidia(const char *device,
    pix_t radius, blkidx_t *nblkp, box_kernel_t *bkp)
{
	if (radius == 1) {
		*nblkp = 256;
		*bkp = BK_MANUAL;
	} else if (radius <= 5) {
		*nblkp = 128;
		*bkp = BK_DIRECT;
	} else if (radius <= 9) {
		*nblkp = 256;
		*bkp = BK_DIRECT;
	} else {
		*nblkp = 256;
		*bkp = BK_SUBBLOCK;
	}
}

/* ------------------------------------------------------------------ */

#define	BOX_PARAMS_VENDOR_INTEL		"Intel Inc."
#define	BOX_PARAMS_VENDOR_AMD		"AMD"
#define	BOX_PARAMS_VENDOR_NVIDIA	"NVIDIA Corporation"

/*
 * This is invoked directly from box_init(), rather than via the usual
 * module mechanism, because it has to come after opencl_preinit() but
 * before the rest of box_init(), and the module mechanism doesn't
 * allow enough control to guarantee that ordering.
 */
void
boxparams_init(void)
{
	const char	*vendor_name = opencl_device_vendor();
	const char	*device_name = opencl_device_name();
	void		(*fn)(const char *, pix_t, blkidx_t *, box_kernel_t *);
	blkidx_t	nblk;
	box_kernel_t	bk;

	if (strcmp(vendor_name, BOX_PARAMS_VENDOR_INTEL) == 0) {
		fn = boxparams_init_intel;
	} else if (strcmp(vendor_name, BOX_PARAMS_VENDOR_AMD) == 0) {
		fn = boxparams_init_amd;
	} else if (strcmp(vendor_name, BOX_PARAMS_VENDOR_NVIDIA) == 0) {
		fn = boxparams_init_nvidia;
	} else {
		verbose(DB_BOX, "Unknown graphics card vendor %s - going with "
		    "default parameters\n", vendor_name);
		fn = boxparams_init_intel;
	}

	for (int radius = 1; radius <= MAX_RADIUS; radius++) {
		(*fn)(device_name, radius, &nblk, &bk);
		boxparams_set(radius, nblk, bk);
	}
}

/*
 * This is used by the box blur performance test, to force all radii to use
 * a given block size and box kernel.
 */
void
boxparams_init_manual(blkidx_t nblk, box_kernel_t bk)
{
	for (int radius = 1; radius <= MAX_RADIUS; radius++) {
		boxparams_set(radius, nblk, bk);
	}
}
