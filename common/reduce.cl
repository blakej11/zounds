/*
 * skip.cl - a computational kernel to help the image skipping code in skip.c.
 */

/*
 * Pick one component out of a vector.
 */
static float
datavec_pick(const datavec v, int dim)
{
	const datavec	masks[DATA_DIMENSIONS] = {
#if	DATA_DIMENSIONS == 4
		(datavec)(1, 0, 0, 0),
		(datavec)(0, 1, 0, 0),
		(datavec)(0, 0, 1, 0),
		(datavec)(0, 0, 0, 1)
#elif	DATA_DIMENSIONS == 3
		(datavec)(1, 0, 0),
		(datavec)(0, 1, 0),
		(datavec)(0, 0, 1)
#elif	DATA_DIMENSIONS == 2
		(datavec)(1, 0),
		(datavec)(0, 1)
#elif	DATA_DIMENSIONS == 1
		(datavec)(1)
#else
#error	I do not know what to do.
#endif
	};

	return (dot(v, masks[dim]));
}

/*
 * Reduce the image to a much smaller one.  The "reduce" argument gives the
 * size of one edge of the reduced image.
 *
 * The original image can be thought of as being tiled with "reduce * reduce"
 * rectangular regions of (nearly) equal size.  The "id" of each pixel
 * indicates which region it is a part of.  All the data points from each
 * region get summed up into a single value of the "result" array.
 *
 * "temp" must be an array of one datavec for each workgroup member.
 * "ids"  must be an array of one int     for each workgroup member.
 *
 * This assumes that all datavec entries are in the range [min, max).
 */
__kernel void
reduce(
	const pix_t		W,	/* in: width of image */
	const pix_t		H,	/* in: height of image */
	__read_only image2d_t	data,	/* in: image */
	__local datavec		*temp,	/* local memory, see above */
	__local int		*ids,	/* local memory, see above */
	const pix_t		reduce,	/* in: size of target edge */
	const int		dim,	/* in: which data dimension to use */
	const float		min,	/* in: minimum value for datavec */
	const float		max,	/* in: maximum value for datavec */
	__global int		*result) /* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);
	const int	w = get_local_size(0);
	const int	h = get_local_size(1);

	if (X >= W || Y >= H) {
		/*
		 * All worker threads need to go through the barrier the
		 * exact same number of times. So if the image is not an
		 * exact multiple of the workgroup size, there will be some
		 * workers that satisfy the above if-test, and we need to
		 * let them just stroll through the barriers without doing
		 * any actual work.
		 */
		barrier(CLK_LOCAL_MEM_FENCE);

		for (int i = 1; i < w; i <<= 1) {
			barrier(CLK_LOCAL_MEM_FENCE);
			barrier(CLK_LOCAL_MEM_FENCE);
		}

		for (int i = 1; i < h; i <<= 1) {
			barrier(CLK_LOCAL_MEM_FENCE);
			barrier(CLK_LOCAL_MEM_FENCE);
		}

		return;
	}

	const int	x = get_local_id(0);
	const int	y = get_local_id(1);
	const int	p = y * w + x;
	const int	id = ((Y * reduce) / H) * reduce + (X * reduce) / W;

	datavec		t;

	temp[p] = as_datavec(read_imagef(data, (int2)(X, Y)));
	ids[p] = id;
	barrier(CLK_LOCAL_MEM_FENCE);

	/*
	 * Sum together all values in this workgroup row with the same ID.
	 */
	for (int i = 1; i < w; i <<= 1) {
		if (((x & i) == 0) && (x + i < w) && ids[p + i] == id) {
			t = temp[p + i];
			ids[p + i] = -1;
			temp[p] += t;
		}
		barrier(CLK_LOCAL_MEM_FENCE);

		if (((x & i) == 0) && (x > i) && ids[p - i] == id) {
			t = temp[p - i];
			ids[p - i] = -1;
			temp[p] += t;
		}
		barrier(CLK_LOCAL_MEM_FENCE);
	}

	/*
	 * Sum together all values in this workgroup column with the same ID.
	 */
	for (int i = 1; i < h; i <<= 1) {
		const int	wi = w * i;

		if (((y & i) == 0) && (y + i < h) && ids[p + wi] == id) {
			t = temp[p + wi];
			ids[p + wi] = -1;
			temp[p] += t;
		}
		barrier(CLK_LOCAL_MEM_FENCE);

		if (((y & i) == 0) && (y > i) && ids[p - wi] == id) {
			t = temp[p - wi];
			ids[p - wi] = -1;
			temp[p] += t;
		}
		barrier(CLK_LOCAL_MEM_FENCE);
	}

	if (ids[p] != -1) {
		/*
		 * Doing the conversion to "int" here means that we'll
		 * be somewhat less precise than if we added together
		 * all the values as floats and then scaled the result
		 * to an integer.  But since we're only look at the top
		 * few bits of the result, it doesn't really matter.
		 * (And there isn't an atomic_add() that works with
		 * floating-point values.)
		 */
		const float	res = datavec_pick(temp[p], dim);
		const float	scaled = (res - min) / (max - min);

		atomic_add(&result[id], (int)(scaled * 255.0));
	}
}
