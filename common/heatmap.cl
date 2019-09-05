/*
 * heatmap.cl - computational kernels for generating the heatmap
 * (multi-dimensional histogram).
 *
 * There are three steps to the process: zeroing out the histogram,
 * creating a histogram from the image, and rendering the histogram.
 *
 * Datasets that have more than two dimensions need to be projected down
 * into 2-space in order to fit on a computer monitor.  We use other code
 * (in basis.c) to generate a pair of perpendicular basis vectors, and
 * take the dot product of each point in data set with each of those
 * vectors to generate the X and Y coordinates for that point.
 */

/*
 * Step 1: zero out the heatmap.
 */
__kernel void
hm_zero(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	__global unsigned int	*heatmap)	/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);
	const pix_t	pix = Y * W + X;

	if (X < W && Y < H) {
		heatmap[pix] = 0;
	}
}

/*
 * Step 2: generate a histogram of "image" into "heatmap", using the vectors
 * "basis1" and "basis2" as a basis for the 2-space to project onto.
 * "min" and "max" are the minimum and maximum values for this data,
 * and "scale" is a multiplier to be used in case the data isn't sphere-like.
 */
__kernel void
hm_histogram(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	float			min,		/* in */
	float			max,		/* in */
	float			scale,		/* in */
	datavec			basis1,		/* in */
	datavec			basis2,		/* in */
	__read_only image2d_t	image,		/* in */
	__global unsigned int	*heatmap)	/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);

	if (X < W && Y < H) {
		/*
		 * Read the data point.
		 *
		 * This code wants the data to be in the range [-1, 1], because
		 * the basis vectors can be anywhere in [-1, 1].  So we rescale
		 * it using min and max.
		 */
		const int2	coord = (int2)(X, Y);
		const float	range = max - min;
		const datavec	rawdat = as_datavec(read_imagef(image, coord));
		const datavec	datum = ((rawdat - min) / range) * 2.0f - 1.0f;

		/*
		 * Choose two components of the data point.
		 */
		const float	f1 = dot(datum, basis1) * scale;
		const float	f2 = dot(datum, basis2) * scale;

		/*
		 * Translate those components into a histogram index.
		 * The "+1.0f" adds back in the local range min "-1", and
		 * the  "2.0f" divides by the local range [-1, 1].
		 */
		const pix_t	b1 = (pix_t)(((f1 + 1.0f) * W) / 2.0f);
		const pix_t	b2 = (pix_t)(((f2 + 1.0f) * H) / 2.0f);
		const pix_t	c1 = clamp(b1, (pix_t)0, W - 1);
		const pix_t	c2 = clamp(b2, (pix_t)0, H - 1);
		const pix_t	idx = ((H - 1) - c2) * W + c1;

		/*
		 * Finally, increment that index in the heatmap.
		 */
		(void) atomic_add(&heatmap[idx], 1);
	}
}

/*
 * Any bucket with more than (1 << MAX_SCALE_SHIFT) entries gets clamped.
 */
#define	MAX_SCALE_SHIFT		9

/*
 * Step 3: render the heatmap into a nice image.
 */
__kernel void
hm_render(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	__global unsigned int	*heatmap,	/* in */
	float			hmscale,	/* in */
	__write_only image2d_t	image)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);
	const pix_t	pix = Y * W + X;

	if (X >= W || Y >= H) {
		return;
	}

	/*
	 * Figure out where the results should go.  If we're in "picture in
	 * picture" mode, stick the heatmap in the lower right-hand corner of
	 * the overall image.
	 */
	const int	x = W * (1 - hmscale) + X * hmscale;
	const int	y = H * (1 - hmscale) + Y * hmscale;

	const float	count = (float)heatmap[pix];
	float		h, s, v;

	/*
	 * HSV values must be in the range [0, 1].
	 */
	if (count < 1) {			/* no hits -> black pixel */
		h = 0.0;
		s = 0.0;
		v = 0.0;
	} else {				/* got some */
		/*
		 * Heat is based on log_2 of the value of this bucket.
		 */
		const float	maxscale = (float)MAX_SCALE_SHIFT;
		const float	heat = min(log2(count) / maxscale, 1.0f);

		/*
		 * Transforming the heat into a target hue:
		 *
		 * - We want to walk around the hue wheel in a way that
		 *   we get "hotter" colors with larger "heat" values.
		 *   That's in the opposite direction from increasing
		 *   numbers.
		 *
		 * - The "heat" gets scaled to [0, 0.9) so that when
		 *   it's transformed into a hue, we have a gap between
		 *   the "coldest" and "hottest" data points.
		 *
		 * - The value 0.62 was empirically chosen to make low
		 *   values look light blue-green (easily visible against
		 *   a black background) and high values look dark blue.
		 */
		h = fmod((1 - heat) * 0.9f + 0.62f, 1.0f);
		s = 1.0;
		v = 1.0;
	}

	hsv_to_image(x, y, (float3)(h, s, v), image);
}
