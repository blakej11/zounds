/*
 * interp.cl - computational kernel for interpolating between two successive
 * images.
 *
 * "start" and "end" are both vectors in the range [min, max).
 */
__kernel void
interpolate(
	const pix_t		W,	/* in: width of image */
	const pix_t		H,	/* in: height of image */
	__read_only image2d_t	start,	/* in: first image */
	__read_only image2d_t	end,	/* in: second image */
	const float		amount,	/* in: how much to interpolate */
	const float		min,	/* in: minimum value for datavec */
	const float		max,	/* in: maximum value for datavec */
	__write_only image2d_t	result)	/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);
	const int2	pos = (int2)(X, Y);

	if (X < W && Y < H) {
		const datavec	s = as_datavec(read_imagef(start, pos));
		const datavec	e = as_datavec(read_imagef(end, pos));
		const datavec	i = fmod(s + (e - s) * amount, max - min);

		write_imagef(result, pos, pack_float4(i));
	}
}
