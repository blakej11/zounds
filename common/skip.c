/*
 * skip.c - manages image skipping.
 *
 * Some configurations of some core algorithms lead to streams of images that
 * alternate between a handful of stable-ish states.  In practice, what this
 * looks like is that the resulting "video" looks extremely flickery.  This
 * code adds a filter so that we can choose to only look at every Nth image,
 * and has code to auto-detect the best image skipping rate (since that can
 * change quickly as other parameters are auto-tuned).
 *
 * Most of this file is devoted to the auto-detect code.  It has three steps:
 *
 * - Given a new image, construct a highly reduced version of the image.
 *   "Highly reduced" is REDUCE x REDUCE pixels, which each reduced pixel
 *   is the average of all the pixels in the corresponding rectangle of the
 *   original image.
 *
 * - Create a hash of the reduced image.  Here, we take the top HASHBITS bits
 *   of the brightness of each pixel and concatenate them.  This is similar to
 *   some of the image hashing algorithms described at:
 *
 *   http://hackerfactor.com/blog/index.php?/archives/432-Looks-Like-It.html
 *
 *   This seems to work better than the dHash described there, given the
 *   filtering I'm trying to accomplish.
 *
 * - Compute the Hamming distance between the current image's hash and the
 *   hashes of the previous [ 0..NSKIPS ] images.  Add this distance to a
 *   exponentially decayed average for each skip amount.  Whichever skip amount
 *   has the lowest decayed average Hamming distance is chosen.
 *
 * Some limitations of this approach:
 *
 * - Some configurations are too chaotic for this to handle, which leads to
 *   quick jumps back and forth between different skip amounts.  This causes
 *   the resulting image stream to look "jumpier", because it takes time to
 *   generate and then throw away an extra image.  Hysteresis might be helpful
 *   here.
 *
 * - The most common amount of skipping desired (other than "no skipping") is
 *   every other image.  This means that "nskip = 1" and "nskip = 3" often
 *   have similar scores.  Jumping back and forth between them leads to the
 *   same jumpiness mentioned above.
 *
 * - This currently just uses the brightness (i.e. the value channel) of each
 *   pixel as part of its hash.  There are probably some image streams where
 *   adding in other channels would give better results, but I haven't figured
 *   out a way to balance that out.  Maybe hash each channel separately, and
 *   use the result from whichever channel has the winner farthest ahead of
 *   the second-place result?
 *
 * - This uses OpenCL to perform the image reduction.  This isn't a fantastic
 *   fit for OpenCL, since it involves coordinating results across many pixels,
 *   but it seems to perform better (on my laptop) than doing it on the CPU.
 *   The final step of the reduction kernel is to add the intermediate result
 *   to a global tracking array, using atomic_add().  But OpenCL can only use
 *   atomic_add() on integral data types.  So, rather than having a nicely
 *   general image reduction kernel, the last step picks out one component
 *   of the vector, scales it up by 256, converts it to an integer, and adds
 *   that to the tracking array.
 */
#include <strings.h>
#include <assert.h>

#include "common.h"

#include "debug.h"
#include "interp.h"
#include "module.h"
#include "opencl.h"
#include "param.h"
#include "skip.h"
#include "util.h"

#define	REDUCE		16	/* reduce image to REDUCExREDUCE */
#define	HASHBITS	4	/* hash this many bits (must be 2^n) */
#define	NSKIPS		3	/* max number of skips to auto-detect */
#define SKIPFADE	0.95	/* decayed moving average of autoskip */

static void	skip_adjust(void);

/* ------------------------------------------------------------------ */

#define	HASHSZ		(P2ROUNDUP(REDUCE * REDUCE * HASHBITS, 8) / 8)
#define	NHASHES		(NSKIPS + 2)

typedef struct {
	uint8_t		hash[HASHSZ];
} skiphash_t;

static struct {
	param_id_t	id;		/* parameter ID for skip count */
	int		param;		/* value of skip parameter */
	int		nskip;		/* number of images to skip */

	kernel_data_t	reduce_kernel;	/* reduction kernel */
	cl_mem		reduce_gpu;	/* memory for reduction */
	int		*reduce_cpu;	/* memory for reduction */

	skiphash_t	hashes[NHASHES]; /* image hashes */
	int		nexthash;	/* which of hashes[] to use next */
	float		score[NHASHES - 1]; /* scores of the skip counts */
} Skip;

/* ------------------------------------------------------------------ */

static void
skip_preinit(void)
{
	param_init_t	pi;

	debug_register_toggle('S', "image skipping", DB_SKIP, NULL);

	bzero(&pi, sizeof (pi));

	/*
	 * The image skipping parameter can be set to skip any number of
	 * images from 0 to NSKIPS.  If the parameter is set to -1, that
	 * triggers the auto-detect code.  Its default setting is to use
	 * autodetect.
	 */
	pi.pi_min = -1;
	pi.pi_default = -1;
	pi.pi_max = NSKIPS;
	pi.pi_units = 1;
	pi.pi_ap_freq = AP_FREQ_OFF;
	pi.pi_ap_rate = AP_RATE_OFF;
	Skip.id = param_register("image skipping", &pi);

	param_key_register('e', KB_DEFAULT, Skip.id, -1);
	param_key_register('E', KB_DEFAULT, Skip.id,  1);

	param_cb_register(Skip.id, skip_adjust);
}

static void
skip_init(void)
{
	const size_t	reducesz = (size_t)REDUCE * REDUCE * sizeof (int);

	kernel_create(&Skip.reduce_kernel, "reduce");

	Skip.reduce_gpu = buffer_alloc(reducesz);
	Skip.reduce_cpu = mem_alloc(reducesz);

	bzero(&Skip.hashes, sizeof (Skip.hashes));

	/* skip_analyze() starts by incrementing this value. */
	Skip.nexthash = -1;
}

static void
skip_fini(void)
{
	buffer_free(&Skip.reduce_gpu);
	mem_free((void **)&Skip.reduce_cpu);

	kernel_cleanup(&Skip.reduce_kernel);
}

const module_ops_t	skip_ops = {
	skip_preinit,
	skip_init,
	skip_fini
};

/* ------------------------------------------------------------------ */

/*
 * This is called when the image skipping parameter is adjusted.
 */
static void
skip_adjust(void)
{
	const int	oval = Skip.param;
	const int	nval = param_int(Skip.id);

	if ((oval < 0) != (nval < 0)) {
		debug(DB_SKIP, "Skip: %sabling auto-detection\n",
		    (nval < 0) ? "en" : "dis");
	}

	Skip.param = nval;
	if (nval >= 0) {
		Skip.nskip = nval;
	} else {
		Skip.nskip = 0;	/* until auto-detection knows better */
	}
}

/*
 * Wrapper around the OpenCL kernel to do image reduction.
 * The "data" is an image2d_t.
 */
static void
skip_reduce(cl_mem data, int dim, float min, float max)
{
	const size_t		reducesz =
	    (size_t)REDUCE * REDUCE * sizeof (int);
	kernel_data_t	*const	kd = &Skip.reduce_kernel;
	pix_t			reduce = REDUCE;
	int			arg;
	int			x, y, p;

	bzero(Skip.reduce_cpu, reducesz);
	buffer_writetogpu(Skip.reduce_cpu, Skip.reduce_gpu, reducesz);

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &data);
	kernel_setarg(kd, arg++, REDUCE * REDUCE * sizeof (cl_datavec), NULL);
	kernel_setarg(kd, arg++, REDUCE * REDUCE * sizeof (int), NULL);
	kernel_setarg(kd, arg++, sizeof (pix_t), &reduce);
	kernel_setarg(kd, arg++, sizeof (int), &dim);
	kernel_setarg(kd, arg++, sizeof (float), &min);
	kernel_setarg(kd, arg++, sizeof (float), &max);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &Skip.reduce_gpu);
	kernel_invoke(kd, 2, NULL, NULL);

	buffer_readfromgpu(Skip.reduce_gpu, Skip.reduce_cpu, reducesz);

	/*
	 * The kernel just adds up the results; here we scale them down to get
	 * an average value for each resulting pixel.  The extra rigmarole for
	 * calculating "dx" and "dy" is because REDUCE might not divide Width
	 * or Height evenly, and we want to get the same pixel count as the
	 * OpenCL kernel uses.
	 */
	for (p = y = 0; y < REDUCE; y++) {
		for (x = 0; x < REDUCE; x++, p++) {
			const int	dx =
			    ((x + 1) * Width / REDUCE) - (x * Width / REDUCE);
			const int	dy =
			    ((y + 1) * Height / REDUCE) - (y * Height / REDUCE);

			Skip.reduce_cpu[p] /= (dx * dy);
		}
	}
}

/*
 * Return the number of bits set in the specified byte.
 */
static int
popc(uint8_t val)
{
	int rv = 0;
	for (int i = 1; i < (1 << 8); i <<= 1) {
		if (val & i) {
			rv++;
		}
	}
	return (rv);
}

/*
 * Create a hash of the most recently reduced image.
 *
 * The CPU-local copy of the image is in Skip.reduce_cpu;
 * the hash goes into Skip.hashes[Skip.nexthash % NHASHES].
 */
static void
skip_hash(void)
{
	const int	idx = Skip.nexthash % NHASHES;
	uint8_t	*const	shash = &Skip.hashes[idx].hash[0];
	uint8_t	*const	ehash = &Skip.hashes[idx].hash[HASHSZ];
	uint8_t		*hash;
	int		x, y, p, newbits, nbits;
	uint8_t		bits;

	bits = 0;
	nbits = 0;
	hash = shash;

	/*
	 * The code below assumes that the bit concatenation will never
	 * wind up spanning multiple bytes.
	 */
	assert(8 % HASHBITS == 0);

	for (p = y = 0; y < REDUCE; y++) {
		for (x = 0, p++; x < REDUCE; x++, p++) {
			int	*n = &Skip.reduce_cpu[p];

			/*
			 * Take the top HASHBITS bits of each pixel and
			 * concatenate them.
			 */
			newbits = n[p] >> (8 - HASHBITS);
			newbits &= ((1 << HASHBITS) - 1);

			bits = (bits << HASHBITS) | newbits;
			nbits += HASHBITS;
			if (nbits == 8) {
				assert(hash < ehash);
				*hash++ = bits;
				bits = 0;
				nbits = 0;
			}
		}
	}

	if (nbits > 0) {
		assert(hash < ehash);
		*hash++ = bits;
	}

	/*
	 * We should have filled up the hash entirely.
	 */
	assert(hash == ehash);
}

/*
 * Analyze the results of the latest hashing.
 */
static int
skip_hashcmp(void)
{
	const int	idx = Skip.nexthash % NHASHES;
	int		nbits, nskip, nh;
	float		bestscore;

	debug(DB_SKIP, "Skip: new scores");

	for (nh = 0; nh < NHASHES - 1; nh++) {
		const int	oidx = (idx - nh - 1 + NHASHES) % NHASHES;
		const uint8_t	*ohash = &Skip.hashes[oidx].hash[0];
		const uint8_t	*nhash = &Skip.hashes[idx].hash[0];

		/*
		 * Compute the Hamming distance between the most recent hash
		 * and the hash "nh" images away.
		 */
		nbits = 0;
		for (int b = 0; b < HASHSZ; b++) {
			nbits += popc(ohash[b] ^ nhash[b]);
		}

		/*
		 * Update the score for this skip count.  This uses
		 * exponential smoothing to feed in new data gently.
		 */
		Skip.score[nh] =
		    Skip.score[nh] * SKIPFADE + nbits * (1 - SKIPFADE);
		debug(DB_SKIP, "%c %7.3f", (nh == 0 ? ':' : ','),
		    Skip.score[nh]);
	}

	/*
	 * Pick the new winner.
	 */
	bestscore = HASHSZ * 8;
	nskip = -1;
	for (nh = 0; nh < NHASHES - 1; nh++) {
		if (Skip.score[nh] < bestscore) {
			bestscore = Skip.score[nh];
			nskip = nh;
		}
	}

	debug(DB_SKIP, " -> best = %d\n", nskip);

	return (nskip);
}

/*
 * The auto-skip detector.
 */
static void
skip_analyze(cl_mem data, int dim, float min, float max)
{
	int	nskip;

	if (Skip.param >= 0) {
		return;		/* not doing auto-skip detection */
	}

	Skip.nexthash++;

	/*
	 * Generate a highly "reduced" version of the data (16x16 pixels).
	 */
	skip_reduce(data, dim, min, max);

	/*
	 * Create a hash of the reduced image.
	 */
	skip_hash();

	if (Skip.nexthash < NHASHES - 1) {
		return;
	}

	/*
	 * See which skip target looks best now, given the new data.
	 */
	nskip = skip_hashcmp();

	if (nskip != Skip.nskip) {
		verbose(DB_SKIP,
		    "Auto-skip detection: now skipping %d image%s\n",
		    nskip, (nskip != 1 ? "s" : ""));
		Skip.nskip = nskip;
	}
}

/*
 * The image skipping engine.  Interposes on core_step().
 */
void
skip_step(cl_mem result, int dim, float min, float max, void (*step)(cl_mem))
{
	const int	nskip = Skip.nskip;

	/*
	 * Generate "nskip" images, and throw them away (overwrite them).
	 * We do feed each image into the auto-skip detector before
	 * overwriting it, though.
	 */
	for (int i = 0; i < nskip; i++) {
		(*step)(result);
		debug(DB_PERF, " (skip)\n");
		skip_analyze(result, dim, min, max);
	}

	/*
	 * Generate a real image to be displayed.
	 */
	(*step)(result);
	skip_analyze(result, dim, min, max);
}
