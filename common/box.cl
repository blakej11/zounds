/*
 * box.cl - two kernels for different implementations of box blur.  They are
 * carefully tied to the corresponding C code in box.c, and should not be
 * thought of as APIs on their own.
 */

#define	PIXEL(x,y,w)	(((y) * (w)) + (x))
#define	WRAP(x,max)	(((x) + (max)) % (max))

/* ------------------------------------------------------------------ */

/*
 * Manual box blur: Each kernel invocation pulls out the N values closest to
 * the current pixel, adds them up, and divides by N.  This is only used for
 * radius 1 (i.e. N = 9).  This is the only "2-D" implementation; the others
 * take advantage of the decomposability of the box blur operation, and need
 * to be done twice to have the correct effect.
 *
 * This takes a fourth ("radius") argument, so it can have the same interface
 * as the other kernels, but it's ignored.
 *
 * Requires temp to be an array of (w * h) boxvector's.
 */
__kernel void
manual_box_2d_r1(
	const pix_t		W,		/* in: actual width */
	const pix_t		H,		/* in: actual height */
	__global boxvector	*in,
	__global boxvector	*out,
	__local boxvector	*temp,
	const pix_t		ignored)
{
	const pix_t		X = get_global_id(0);
	const pix_t		Y = get_global_id(1);
	const pix_t		r = 1;
	const float		scale = (2 * r + 1);
	const bool		inbounds = (X < W && Y < H);
	boxvector		acc;

	if (inbounds) {
		acc = 0;
		for (spix_t y = Y - r; y <= (spix_t)(Y + r); y++) {
			const pix_t	ty = WRAP(y, H);
			for (spix_t x = X - r; x <= (spix_t)(X + r); x++) {
				acc += in[PIXEL(WRAP(x, W), ty, W)];
			}
		}

		temp += get_local_id(1) * get_local_size(0) + get_local_id(0);
		*temp = acc / (scale * scale);
	}

	barrier(CLK_LOCAL_MEM_FENCE);

	if (inbounds) {
		out[PIXEL(X, Y, W)] = *temp;
	}
}

/* ------------------------------------------------------------------ */

/*
 * Direct box blur: Each kernel invocation pulls the 2 * R + 1 values out of
 * the current row, averages them, and puts the resulting value in the output
 * array.  This only operates on rows, because all the values in a given row
 * are located adjacent to each other in memory, so the data gets streamed
 * nicely into the GPU cores.
 *
 * The result is placed in the output array in the location equal to the source
 * location with X and Y transposed; this way, both passes of the box blur can
 * use the same code.  In practice, the store of the result into the output
 * array takes as much time as the entire rest of the kernel, because the
 * results aren't nicely streamed out.  When the workgroup size is a square,
 * we can use a little trick that lets us get a bit of streaming, as described
 * below.
 *
 * Requires temp to be an array of (w * h) boxvector's.
 */
__kernel void
direct_box_1d(
	const pix_t		W,		/* in: actual width */
	const pix_t		H,		/* in: actual height */
	__global boxvector	*in,
	__global boxvector	*out,
	__local boxvector	*temp,
	const pix_t		r)
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);
	const pix_t	w = get_local_size(0);
	const pix_t	h = get_local_size(1);
	const pix_t	x = get_local_id(0);
	const pix_t	y = get_local_id(1);
	const float	scale = (2 * r + 1);
	const bool	inbounds = (X < W && Y < H);
	const bool	all_inbounds = ((X - x + w) <= W && (Y - y + h) <= H);
	boxvector		acc;
	__global boxvector	*inrow;

	acc = 0;

	if (inbounds) {
		inrow = &in[PIXEL(0, Y, W)];
		for (spix_t i = X - r; i <= (spix_t)(X + r); i++) {
			acc += inrow[WRAP(i, W)];
		}
		acc /= scale;
	}

	/*
	 * The 1-D box blur is fundamentally similar to a transposing copy
	 * operation, where the first row of the source array is (blurred
	 * and then) turned into the first column of the destination array.
	 *
	 * A naive approach to a transposing copy, where each worker reads one
	 * value from location (X,Y) in the source and writes it to location
	 * (Y,X) in the destination, winds up bottlenecking on memory access
	 * speed. This happens because the nicely pipelined accesses to
	 * sequentially increasing addresses from the source array get turned
	 * into bank-contending addresses in the destination array, at least
	 * if the width of the image is some nice round number of pixels wide.
	 * (And even if it isn't, you won't get cache merging if the stores
	 * don't target sequential addresses.)
	 *
	 * This isn't the ony source of slowdown for a box blur, because the
	 * multiple memory loads from the source array take more time than
	 * the single load done by a simple transposing copy.  But it's worth
	 * improving this when we can.
	 *
	 * We can do something slightly more clever when the two sides of the
	 * workgroup are equal in length. In that case, we're still going to
	 * be taking the source block with upper-left-hand coordinates (X,Y)
	 * and (blurring and then) copying it to the destination in the
	 * position with upper-left-hand coordinates (Y,X). And the block
	 * itself will be transposed. The clever bit comes from storing the
	 * transposed block temporarily in (much faster) local memory, where
	 * the cost of non-sequential stores is much lower. After doing that,
	 * we can do another pass to copy it from local memory to the
	 * destination array using sequential stores.
	 *
	 * For example, imagine the following part of a source array,
	 * where the upper-left-hand entry "A" is at coordinate (X,Y):
	 *
	 *	.  .  .  .  .  .  .  .  .  .
	 *	.  .  .  A  B  C  D  .  .  .
	 *	.  .  .  E  F  G  H  .  .  .
	 *	.  .  .  I  J  K  L  .  .  .
	 *	.  .  .  M  N  O  P  .  .  .
	 *	.  .  .  .  .  .  .  .  .  .
	 *
	 * In this example, the workgroup is 4 by 4 in size, so both X and Y
	 * are integer multiples of 4.
	 *
	 * The thread corresponding to entry "B" (i.e. for source location
	 * (X+1,Y)) loads some other values from that same row, averages them
	 * together (producing "B'"), and stores that value into location (0,
	 * 1) in the temporary array. After all threads in the workgroup have
	 * executed this work, they execute a memory barrier, and the local
	 * memory array looks like this:
	 *
	 * 	A' E' I' M'
	 * 	B' F' J' N'
	 * 	C' G' K' O'
	 * 	D' H' L' P'
	 *
	 * Once we've reached this point, the thread for source location
	 * (X+1,Y) can now load the value at (1, 0) from the temp array,
	 * yielding "E'", and store that value into location (Y+1,X) in the
	 * destination array.
	 *
	 * For the code below which executes this, note a couple things:
	 *
	 * - The workgroup starts at pixel (X - x, Y - y). The loop above
	 *   accumulates data around pixel (X, Y), but that could also be
	 *   written as pixel ((X - x) + x, (Y - y) + y). The final store
	 *   below deposits data into ((Y - y) + x, (X - x) + y), which
	 *   transposes the workgroup's coordinates but keeps the thread's
	 *   coordinates within the workgroup.
	 *
	 * - We only execute this "clever store" if all threads in the
	 *   workgroup are computing values that are inside the (W, H)
	 *   bounds for the overall image.
	 */

	if (w == h && all_inbounds) {		// clever store
		temp[PIXEL(y, x, h)] = acc;
		barrier(CLK_LOCAL_MEM_FENCE);
		out[PIXEL(Y - y + x, X - x + y, H)] = temp[PIXEL(x, y, w)];
	} else if (inbounds) {			// slow store
		out[PIXEL(Y, X, H)] = acc;
	}
} 
#undef	PIXEL
#undef	WRAP
