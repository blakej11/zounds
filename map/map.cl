/*
 * RGB values are in the range [ 0.0, 1.0 ]
 * XYZ values are in the range [ 0.0, 1.0 ]
 */
__kernel void
unrender(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	__read_only image2d_t	image,		/* in */
	__write_only image2d_t	data)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);

	if (X < W && Y < H) {
		const float3	rgb = image_to_rgb(X, Y, image);
		write_imagef(data, (int2)(X, Y), (float4)(rgb, 0));
	}
}

/*
 * Just copy the data from the image2d_t into the datavec array.
 */
__kernel void
import(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	__read_only image2d_t	src,		/* in */
	__global datavec	*dst)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);

	if (X < W && Y < H) {
		dst[Y * W + X] = as_datavec(read_imagef(src, (int2)(X, Y)));
	}
}

__kernel void
export(
	const pix_t		W,		/* in */
	const pix_t		H,		/* in */
	__global datavec	*src,		/* in */
	__write_only image2d_t	dst)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);

	if (X < W && Y < H) {
		write_imagef(dst, (int2)(X, Y), as_float4(src[Y * W + X]));
	}
}

/*
 * XYZ values are in the range [ 0.0, 1.0 ]
 * RGB values are in the range [ 0.0, 1.0 ]
 */
__kernel void
render(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	__read_only image2d_t	data,		/* in */
	__write_only image2d_t	image)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);

	if (X < W && Y < H) {
		rgb_to_image(X, Y, read_imagef(data, (int2)(X, Y)).xyz, image);
	}
}
