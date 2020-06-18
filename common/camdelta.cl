/* ------------------------------------------------------------------
 * Code for reading from webcam images. They are stored as packed BGR
 * data, and may have a different resolution than the displayed image.
 */

#define	MIN(a,b)	((a) < (b) ? (a) : (b));

/*
 * This uses the algorithm from image_copy() to only take whatever portion
 * of the camera data that will fit into the result image. It also converts
 * the data from packed-char BGR-order to a float3 in RGB-order.
 */
static float3
load_from_bgr(
	const pix_t		iW,		/* in - image width */
	const pix_t		iH,		/* in - image height */
	const pix_t		rW,		/* in - result width */
	const pix_t		rH,		/* in - result height */
	__global uchar		*image)		/* in - packed BGR */
{
	const pix_t		X = get_global_id(0);
	const pix_t		Y = get_global_id(1);
        const pix_t		idx = (rW < iW ? (iW - rW) / 2 : 0);
        const pix_t		rdx = (rW > iW ? (rW - iW) / 2 : 0);
        const pix_t		idy = (rH < iH ? (iH - rH) / 2 : 0);
        const pix_t		rdy = (rH > iH ? (rH - iH) / 2 : 0);
        const pix_t		w = MIN(iW, rW);
        const pix_t		h = MIN(iH, rH);

	if (X < rdx || X >= rdx + w || Y < rdy || Y >= rdx + h) {
		return ((float3)0);
	}

	const pix_t		iX = X - rdx + idx;
	const pix_t		iY = Y - rdy + idy;
	const pix_t		ip = iY * iW + iX;
	uchar3			t;
	float3			rgb;

	t = vload3(ip, image);

	rgb.x = (float)t.z / 255.0;
	rgb.y = (float)t.y / 255.0;
	rgb.z = (float)t.x / 255.0;

	return (rgb);
}

#undef	MIN

/*
 * Perform difference detection on successive camera images.
 *
 * The result gets a copy of the old image (converted to RGB) in its XYZ
 * components, and the Euclidean distance in RGB-space between old and new
 * images in its W component. All four vector components (XYZW) are in the
 * range [0, 1]. Other pieces of code compare the computed distance to some
 * threshold and decide whether to use the old image or to ignore it.
 *
 * We return the old image rather than the new one because that lets us
 * capture the final frame of movement before something "reverts to
 * background", and thereby get the agents to seed themselves based on the
 * color of the movement rather than the background color.
 */
__kernel void
camera_delta(
	const pix_t		iW,		/* in - image width */
	const pix_t		iH,		/* in - image height */
	__global uchar		*oimage,	/* in - packed BGR */
	__global uchar		*nimage,	/* in - packed BGR */
	const pix_t		rW,		/* in - result width */
	const pix_t		rH,		/* in - result height */
	__write_only image2d_t	result)		/* out - delta as HSV */
{
	const pix_t		X = get_global_id(0);
	const pix_t		Y = get_global_id(1);

	if (X >= iW || Y >= iH) {
		return;
	}

	const float3		orgb = load_from_bgr(iW, iH, rW, rH, oimage);
	const float3		nrgb = load_from_bgr(iW, iH, rW, rH, nimage);

	const float		dx = orgb.x - nrgb.x;
	const float		dy = orgb.y - nrgb.y;
	const float		dz = orgb.z - nrgb.z;
	const float		parts = dx * dx + dy * dy + dz * dz;
	const float		d = sqrt(parts) / sqrt(3.0f);

	const datavec		res = (datavec)(rgb_to_hsv(orgb), d);

	/* "rW - X" flips the camera image around horizontally. */
	write_imagef(result, (int2)(rW - X, Y), pack_float4(res));
}
