/*
 * render.cl - the computational kernels for rendering the 1-D MSTP algorithm.
 *
 * Yep, this is all it takes.  The Makefile pulls in multiscale.{c,cl} and
 * tweak.c from the 4-D MSTP implementation.
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
		/* HSV values are in [0, 1], but MSTP uses data in [-1, 1]. */
		const float	v = 2 * image_to_hsv(X, Y, image).z - 1;
		write_imagef(data, (int2)(X, Y), (float4)v);
	}
}

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
render(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	int			rendertype,	/* ignored */
	__read_only image2d_t	data,		/* in */
	__global float		*recentscale,	/* ignored */
	__write_only image2d_t	image)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);

	if (X < W && Y < H) {
		/* Our data is in [-1, 1], but HSV values must be in [0, 1]. */
		const float	v = as_datavec(read_imagef(data, (int2)(X, Y)));
		hsv_to_image(X, Y, (float3)(0, 0, (v + 1) / 2), image);
	}
}
