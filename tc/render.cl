/*
 * render.cl - computational kernels for rendering/unrendering the colorized
 * version of Multi-Scale Turing Patterns.
 */

/*
 * RGB  values are in the range [  0.0, 1.0 ]
 * HSV  values are in the range [  0.0, 1.0 ]
 * XYZW values are in the range [ -1.0, 1.0 ]
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
	const int2	coord = (int2)(X, Y);

	if (X >= W || Y >= H) {
		return;
	}

	const float3	hsv = image_to_hsv(X, Y, image);

	/*
	 * Since there are only three HSV values but the vector is 4-D,
	 * we arbitrarily set the radius to 1.  Using a random value instead
	 * adds random noise and makes it look worse.
	 *
	 * See render() for the details of why this is the mapping.
	 */
	const float	rho = 1.0;
	const float	theta1 = hsv.z / 1.01f;		/* [  0.0, 1.0 ] */
	const float	theta2 = hsv.y;			/* [  0.0, 1.0 ] */
	const float	phi = (hsv.x * 2.0) - 1.0;	/* [ -1.0, 1.0 ] */

	const datavec	result = (datavec)(
	    rho * cospi(theta1),
	    rho * sinpi(theta1) * cospi(theta2),
	    rho * sinpi(theta1) * sinpi(theta2) * cospi(phi),
	    rho * sinpi(theta1) * sinpi(theta2) * sinpi(phi));

	write_imagef(data, coord, result);
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

/*
 * XYZW values are in the range [ -1.0, 1.0 ]
 * RGB  values are in the range [  0.0, 1.0 ]
 */
__kernel void
render(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	int			rendertype,	/* in */
	__read_only image2d_t	data,		/* in */
	__global float		*recentscale,	/* in */
	__write_only image2d_t	image)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);

	if (X >= W || Y >= H) {
		return;
	}

	const datavec	datum = as_datavec(read_imagef(data, (int2)(X, Y)));

	/*
	 * Take the 4-vector, and turn it into 4-spherical coordinates.
	 *
	 * (calculating rhox as sqrt(rho^2 - x^2) leads to too much FP error)
	 */
	const float	rho = min(length(datum), 1.0f);
	const datavec	rx = (datavec)(0.0f, datum.y, datum.z, datum.w);
	const float	rhox = min(length(rx), 1.0f);

	/* theta1 and theta2 are in [ 0, 1 ].  phi is in [ -1, 1 ]. */
	const float	theta1 = (rho  != 0.0f ? acospi(datum.x / rho)  : 0);
	const float	theta2 = (rhox != 0.0f ? acospi(datum.y / rhox) : 0);
	const float	phi = atan2pi(datum.w, datum.z);

	/*
	 * Use the 4-spherical coordinates to get HSV values.
	 *
	 * It seems to be more visually interesting to only use the angles,
	 * rather than using the radius (rho). I don't believe there's a
	 * fundamental reason for this, it's just been my experience after
	 * a lot of experimenting.
	 *
	 * atan2() is more uniformly distributed than acos(), so that one
	 * goes to the hue channel to get a nice wide color palette.
	 *
	 * theta1 is problematic, because the Y, Z, and W channels all have a
	 * " * sinpi(theta1)" term in their expressions.  So any vector with
	 * theta1 = 0 or 1 will wind up as <+/- 1, 0, 0, 0>.  This is fine
	 * for theta1 = 0, because that maps onto v = 0, and all points
	 * with minimum value are equally black.  But if theta1 = 1 mapped
	 * onto v = 1, then all points with R = 255, G = 255, or B = 255
	 * would map onto white.
	 *
	 * To work around this, the unrender() code maps value-space onto
	 * theta1 values from 0 to X=(1/1.01). If we encounter an actual
	 * theta1 value that's larger than X, we map it to the same value
	 * as we would for (X - (theta1 - X)).
	 *
	 * This mapping is reversed below in unrender(), so if it's changed
	 * here, it should be changed there as well.
	 *
	 * h, s, and v are all in [0, 1].
	 */
	const float	h = (phi + 1) / 2;
	const float	s = theta2;
	const float	v = 1 - 1.01f * fabs(theta1 - 1 / 1.01f);

	/*
	 * This is mostly debugging code - it lets me explore different
	 * ways of coloring the data. So far I haven't found anything that's
	 * obviously more compelling than the default mode.
	 */
	float rs;
	switch (rendertype & 1) {
	case 0:	// Default.
		hsv_to_image(X, Y, (float3)(h, s, v), image);
		break;

	case 1:	// Recentscale.
		rs = recentscale[Y * W + X];
		hsv_to_image(X, Y, (float3)(rs, 1.0f, 1.0f), image);
		break;
	}
}
