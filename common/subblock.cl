/*
 * subblock.cl -- implementation of 1-D box blur using sub-blocking.
 *
 * This operates on one or more complete rows at time.  Each row is divided
 * into some number of equally sized subblocks, and a workgroup thread is
 * responsible for generating all of the result values in its subblock.
 *
 * The steps for doing this:
 *
 * 1) A thread reads in and adds up all the values of its subblock from the
 *    source array.  These intermediate results are stored in local memory.
 *
 * 2) It adds up all of the neighboring subblocks needed to reach its target
 *    radius.
 *
 * 3) It reads in and adds up any additional values needed from the source
 *    array, if the radius is not a multiple of the subblock size.  This
 *    yields the sum (and thus the average) around the given pixel.
 *
 * 4) To generate the result for the other pixels in the subblock, it subtracts
 *    the value at "r" pixels before the current one, and adds the value at
 *    "r+1" pixels after the current one.  This is wonderfully O(1).
 *
 * This works startlingly well even for radii that are substantial fractions
 * of the image width/height.
 *
 * If the current image isn't a clean multiple of the workgroup size, this
 * task becomes more complicated, and laden with potential off-by-one errors.
 * In that situation we still divide each row into some fixed number of
 * subblocks, but some subblocks will have one more pixel than some others.
 * (The code refers to these as "large" and "small" subblocks, respectively.)
 * As a consequence, each block will have a slightly different number of
 * blocks and extra pixels to add up, both from its left and its right.
 *
 * The "subblock params" code helps with this - it calculates how many blocks
 * and pixels each block will need to accumulate, and builds a table with
 * this information. These calculations are run when the image changes size, 
 * so the resulting table can be accessed quickly when the hard work of doing
 * the box blurs needs to happen.
 */

#define	PIXEL(x,y,w)	(((y) * (w)) + (x))
#define	WRAP(x,max)	(((x) + (max)) % (max))

#include "subblock.h"

/* ------------------------------------------------------------------ */

/*
 * Returns (blk, pix): the number of leftward (blocks, pixels) needed.
 */
static ushort2
subblock_lh_params(
    const blkidx_t b,		/* current block's coordinate */
    blkidx_t nblk,		/* number of blocks in a row */
    pix_t width,		/* width of the image, in pix */
    pix_t radius)		/* radius of the box blur */
{
	const pix_t	bw = width / nblk;	/* block width */
	const blkidx_t	overflow = width % nblk;
	pix_t		pix;			/* # pixels covered */
	blkidx_t	blk;			/* # blocks covered */
	blkidx_t	curblk;			/* current block pointer */
	blkidx_t	curblocks;		/* # blocks to take this time */

	if (overflow == 0) {			/* clean division into blocks */
		curblocks = radius / bw;
		return ((ushort2)(curblocks, radius - curblocks * bw));
	}

	pix = 0;
	blk = 0;

	/*
	 * If there's room to take at least one block, and we're to the right
	 * of the overflow point, use small blocks.
	 */
	curblk = WRAP(b - blk, nblk);
	if (curblk == 0) {
		curblk = nblk;
	}
	if (pix + bw <= radius && curblk > overflow) {
		curblocks = min(curblk - overflow,
		    (blkidx_t)((radius - pix) / bw));
		blk += curblocks;
		pix += curblocks * bw;
	}

	/*
	 * If there's still room to take at least one block, and we're at or
	 * below the overflow point, use large blocks.
	 */
	curblk = WRAP(b - blk, nblk);
	if (pix + (bw + 1) <= radius && curblk <= overflow) {
		curblocks = min(curblk, (blkidx_t)((radius - pix) / (bw + 1)));
		blk += curblocks;
		pix += curblocks * (bw + 1);

		/*
		 * If there's still room to take at least one block,
		 * then we must have wrapped back to the top. Use small blocks.
		 */
		curblk = WRAP(b - blk, nblk);
		if (curblk == 0) {
			curblk = nblk;
		}
		if (pix + bw <= radius && curblk > overflow) {
			curblocks = min(curblk - overflow,
			    (blkidx_t)((radius - pix) / bw));
			blk += curblocks;
			pix += curblocks * bw;

			/*
			 * If there's still room to take at least one block,
			 * and we've wrapped back below the overflow point,
			 * use large blocks.
			 */
			curblk = WRAP(b - blk, nblk);
			if (pix + (bw + 1) <= radius && curblk <= overflow) {
				curblocks = min(curblk,
				    (blkidx_t)((radius - pix) / (bw + 1)));
				blk += curblocks;
				pix += curblocks * (bw + 1);
			}
		}
	}

	return ((ushort2)(blk, radius - pix));
}

/*
 * Returns (blk, pix): the number of rightward (blocks, pixels) needed.
 */
static ushort2
subblock_rh_params(
    const blkidx_t b,		/* current block's coordinate */
    blkidx_t nblk,		/* number of blocks in a row */
    pix_t width,		/* width of the image, in pix */
    pix_t radius)		/* radius of the box blur */
{
	const pix_t	bw = width / nblk;	/* block width */
	const blkidx_t	overflow = width % nblk;
	pix_t		pix;			/* # pixels covered */
	blkidx_t	blk;			/* # blocks covered */
	blkidx_t	curblk;			/* current block pointer */
	blkidx_t	curblocks;		/* # blocks to take this time */

	if (overflow == 0) {			/* clean division into blocks */
		curblocks = radius / bw;
		return ((ushort2)(curblocks, radius - curblocks * bw));
	}

	pix = 0;
	blk = 0;

	/*
	 * If there's room to take at least one block, and we're to the left
	 * of the overflow point, use large blocks.
	 */
	curblk = WRAP(b + blk, nblk);
	if (pix + (bw + 1) <= radius && curblk < overflow) {
		curblocks = min(overflow - curblk,
		    (blkidx_t)((radius - pix) / (bw + 1)));
		blk += curblocks;
		pix += curblocks * (bw + 1);
	}

	/*
	 * If there's still room to take at least one block, and we're at or
	 * above the overflow point, use small blocks.
	 */
	curblk = WRAP(b + blk, nblk);
	if (pix + bw <= radius && curblk >= overflow) {
		curblocks = min(nblk - curblk, (blkidx_t)((radius - pix) / bw));
		blk += curblocks;
		pix += curblocks * bw;

		/*
		 * If there's still room to take at least one block, then we
		 * must have wrapped back to the bottom. Use large blocks.
		 */
		curblk = WRAP(b + blk, nblk);
		if (pix + (bw + 1) <= radius && curblk < overflow) {
			curblocks = min(overflow - curblk,
			    (blkidx_t)((radius - pix) / (bw + 1)));
			blk += curblocks;
			pix += curblocks * (bw + 1);

			/*
			 * If there's still room to take at least one block,
			 * and we've wrapped back above the overflow point,
			 * use small blocks.
			 */
			curblk = WRAP(b + blk, nblk);
			if (pix + bw <= radius && curblk >= overflow) {
				curblocks = min(nblk - curblk,
				    (blkidx_t)((radius - pix) / bw));
				blk += curblocks;
				pix += curblocks * bw;
			}
		}
	}

	return ((ushort2)(blk, radius - pix));
}

__kernel void
subblock_build_table(
	__global subblock_params_t	*params,
	__global blkidx_t		*nblocks_per_radius,
	const pix_t			width)
{
	const blkidx_t	X = get_global_id(0);	// which block to do
	const pix_t	Y = get_global_id(1);	// which radius to do
	const blkidx_t	blk = X;		// another name for it
	const pix_t	radius = Y + 1;		// Y is 0-based
	const blkidx_t	nblocks = nblocks_per_radius[radius - 1];

	subblock_params_t	sbp;

	if (X >= nblocks) {
		/*
		 * These values should never actually be consumed,
		 * so this is just to make them stand out when debugging.
		 */
		sbp.sp_lblk = (short)X;
		sbp.sp_lpix = (short)nblocks;
		sbp.sp_rblk = (short)Y;
		sbp.sp_rpix = 0xdd;
	} else {
		ushort2		lhs, rhs;

		lhs = subblock_lh_params(blk, nblocks, width, radius);
		rhs = subblock_rh_params(blk, nblocks, width, radius);

		sbp.sp_lblk = lhs.x;
		sbp.sp_lpix = lhs.y;
		sbp.sp_rblk = rhs.x;
		sbp.sp_rpix = rhs.y;
	}

	params[PIXEL(X, Y, MAX_NBLOCKS)] = sbp;
}

/* ------------------------------------------------------------------ */

/*
 * The actual subblock box blur code. See the comment at the top of the file
 * for general operating principles.
 *
 * This has the same output-streaming issue as the direct box blur kernel,
 * and uses a similar trick to try to speed it up.
 *
 * Some requirements:
 *
 * - temp must be an array of (w * h) boxvector's.
 *
 * - get_global_size(0) must be set to the value of get_local_size(0)
 *   (rather than setting it to the real width).
 */
__kernel void
subblock_box_1d(
	const pix_t		W,		/* in: actual width */
	const pix_t		H,		/* in: actual height */
	__global boxvector	*in,
	__global boxvector	*out,
	__local boxvector	*temp,
	const pix_t		r,
	__global const subblock_params_t *params) /* in: parameters */
{
	const blkidx_t	w = get_local_size(0);	// workgroup width (# blocks)
	const pix_t	h = get_local_size(1);	// workgroup height
	const blkidx_t	x = get_local_id(0);	// current block index
	const pix_t	y = get_local_id(1);	// current block height
	const pix_t	rawbw = W / w;		// "small" block width
	const blkidx_t	overflow = W % w;	// small->large transition
	const pix_t	bw =			// this block's actual width
	    rawbw + (x < overflow ? 1 : 0);
	const pix_t	X =			// global X position, pixels
	    x * rawbw + min(x, overflow);
	const pix_t	Y = get_global_id(1);	// global Y position, pixels
	const bool	inbounds =		// bounds checks:
	    ((pix_t)x < W && Y < H);		//   x < W is for windows
	const bool	all_inbounds =		//   that are narrower than
	    ((pix_t)x < W && (Y - y + h) <= H);	//   nblocks.

	const subblock_params_t	sbp =		// parameters for this block
	    params[PIXEL(x, r - 1, MAX_NBLOCKS)];
	const blkidx_t	lblk = x - (blkidx_t)sbp.sp_lblk;
	const blkidx_t	rblk = x + (blkidx_t)sbp.sp_rblk;
	const spix_t	lpix = (spix_t)sbp.sp_lpix;
	const spix_t	rpix = (spix_t)sbp.sp_rpix;

	const float	scale = (2 * r + 1);	// scale factor for accum

	__global boxvector	*inrow;
	__local boxvector	*temprow;
	boxvector		accum;
	spix_t			i;

	/*
	 * First: sum up our block into temp.
	 */
	accum = 0;
	temprow = &temp[y * w];
	inrow = &in[PIXEL(X, Y, W)];
	if (inbounds) {
		for (i = 0; i < (spix_t)bw; i++) {
			accum += inrow[i];
		}
	}
	temprow[x] = accum;
	barrier(CLK_LOCAL_MEM_FENCE);

	/*
	 * Next: sum up our necessary blocks from temp.
	 */
	accum = 0;
	if (inbounds) {
		for (blkidx_t b = lblk; b < rblk; b++) {
			accum += temprow[WRAP(b, w)];
		}
	}
	barrier(CLK_LOCAL_MEM_FENCE);

	/*
	 * If our radius doesn't divide cleanly into bw, we need to
	 * accumulate the extra values directly from "inrow".
	 */
	if (inbounds && ((lpix | rpix) != 0)) {
		spix_t	offset;
		inrow = &in[PIXEL(0, Y, W)];

		offset = X - r;
		for (i = 0; i < lpix; i++) {
			accum += inrow[WRAP(offset + i, W)];
		}

		offset = X + r - rpix;
		for (i = 0; i < rpix; i++) {
			accum += inrow[WRAP(offset + i, W)];
		}
	}

	/*
	 * At this point, each thread is "loaded" with its own accumulator,
	 * so we can reuse "temp" to hold the results.
	 *
	 * Note that this loop goes up to the size of the large block, even
	 * though some threads operate on a small block.  This is to ensure
	 * that all threads hit the same barriers in the same places.  The
	 * "if"-tests after the first barrier keep the small blocks from doing
	 * a final (improper) store.
	 */
	inrow = &in[PIXEL(0, Y, W)];
	for (i = 0; i < (spix_t)(rawbw + 1); i++) {
		if (inbounds) {
			accum += inrow[WRAP(X + i + r, W)];
			temp[PIXEL(y, x, h)] = accum / scale;
			accum -= inrow[WRAP(X + i - r, W)];
		}

		barrier(CLK_LOCAL_MEM_FENCE);

		/*
		 * Okay, there's a lot going on here.
		 *
		 * Start by reading the comment at the end of direct_box_1d()
		 * for an introduction to the "clever store".
		 *
		 * The additional tests here are for:
		 *
		 * - "i < rawbw":
		 *
		 *   We're still within the range of both small and large
		 *   blocks.  If this test fails, then we're on the single
		 *   pixel where the large block should act but the small one
		 *   shouldn't.
		 *
		 * - "x < overflow" (for the *slow* store):
		 *
		 *   The code just above computed the box-blurred values, and
		 *   placed them in transposed order into temp[].  If the
		 *   "i < rawbw" test failed and we're a small block, the value
		 *   we computed shouldn't be stored (in particular, the
		 *   rightmost workgroups would store into the column just off
		 *   the right side of the array).  And we're a small block if
		 *   x < overflow.
		 *
		 * - "y < overflow" (for the *clever* store):
		 *
		 *   With the clever store, we read out a different value from
		 *   temp than we stored into it -- we read out the value from
		 *   the non-transposed position.  So we essentially transpose
		 *   the "x < overflow" test, to arrive at "y < overflow".
		 *
		 * The only other additional complication is the value of
		 * "ybase".  It's doing the same operation as we do to compute
		 * X, at the beginning of the function - making the first
		 * "overflow" blocks of y into large blocks, and the rest into
		 * small blocks.  The source coordinate was derived from its
		 * "x", but we need to use the x and y values of the transposed
		 * location to determine its placement in the destination.
		 */
		if ((pix_t)w == h && all_inbounds &&	// clever store
		    (i < (spix_t)rawbw || y < (pix_t)overflow)) {
			const pix_t	ybase =
			    y * rawbw + min(y, (pix_t)overflow);

			out[PIXEL((Y - y) + x, ybase + i, H)] =
			    temp[PIXEL(x, y, w)];
		} else if (inbounds &&			// slow store
		    (i < (spix_t)rawbw || x < overflow)) {
			out[PIXEL((Y - y) + y, X + i, H)] =
			    temp[PIXEL(y, x, h)];
		}

		barrier(CLK_LOCAL_MEM_FENCE);
	}
}

#undef WRAP
#undef PIXEL
