/*
 * multiscale.cl - the computational kernel for Jonathan McCabe's
 * "multi-scale Turing patterns" algorithm, also used for Turing clouds.
 *
 * The rendering/unrendering/importing code is in render.cl, since the 1-D
 * version uses a fundamentally different mapping from data point to color.
 */
__kernel void
multiscale(
	const pix_t		W,		/* in */
	const pix_t		H,		/* in */
	__global boxvector	*d0,		/* in */
	__global boxvector	*d1,		/* in */
	__global boxvector	*d2,		/* in */
	__global boxvector	*d3,		/* in */
	__global boxvector	*d4,		/* in */
	__global boxvector	*d5,		/* in */
	__global boxvector	*d6,		/* in */
	__global boxvector	*d7,		/* in */
	__global boxvector	*d8,		/* in */
	__global float		*adj,		/* in */
	const float		maxadj,		/* in */
	const int		nscales,	/* in */
	__global datavec	*odata,		/* in */
	__global datavec	*ndata,		/* out */
	__global float		*recentscale,	/* in/out */
	__write_only image2d_t	result)		/* out */
{
	const pix_t		X = get_global_id(0);
	const pix_t		Y = get_global_id(1);
	const pix_t		p = Y * W + X;
	__global boxvector	*const	densities[9] =
	    { d0, d1, d2, d3, d4, d5, d6, d7, d8 };

	boxvector		o, n, diff, tgtv;
	datavec			od, nd;
	float			minlen, len;
	int			s, tgts;

	if (X >= W || Y >= H) {
		return;
	}

	minlen = FLT_MAX;
	o = densities[0][p];

	for (s = 1; s < nscales; s++) {
		/*
		 * Look for the adjacent-scale pair that has the
		 * smallest-magnitude difference vector.
		 */
		n = densities[s][p];
		diff = n - o;
		o = n;
		len = length(diff);

		if (len < minlen) {
			minlen = len;
			tgts = s;
			tgtv = diff;
		}
	}

	/*
	 * At this point, "tgts" is the smaller-radius scale index of the
	 * scale pair we've chosen, and "tgtv" is the difference vector
	 * between "s" and "s-1".
	 *
	 * We adjust this data point by the difference vector, as scaled by
	 * the adjustment factor for this scale.
	 */
	od = odata[p];
	nd = od;
	if (minlen > 0) {
		nd += normalize(tgtv) * adj[tgts - 1];
	}

	/*
	 * This algorithm relies on the data staying within a nicely bounded
	 * range - each vector component should be within [-1.0, 1.0].
	 *
	 * One easy fix for this would be to clamp() all values to that
	 * range, but in practice that leaves too many data points stuck at
	 * the extremities of the range (especially in 4-D mode).  Another
	 * approach would be to run another kernel after this one to find
	 * the min/max values, and then yet another to rescale everything,
	 * but min/max isn't as parallelizable as some things.
	 *
	 * Instead, we just observe that the largest possible component
	 * would be (1 + maxadj) -- which would happen if odata[p] was a
	 * unit vector in some direction, normalize(tgtv) was a unit vector
	 * in the same direction, and adj[tgts - 1] used the maximum
	 * adjustment.  So we simplify the process by just forcibly
	 * rescaling all the results by that amount.
	 *
	 * This also injects visually useful instability into the system.
	 */
	nd /= (1 + maxadj);
	ndata[p] = nd;

	/*
	 * A decayed moving average of which scale was used to drive this
	 * pixel's update. Values are in [0,1]. Mostly useful for
	 * debugging.
	 */
	const float	rs = recentscale[p];
	const float	decay = 0.97f;
	const float	ns = decay * rs +
	    (1.0f - decay) * (float)tgts / nscales;
	recentscale[p] = ns;

	/*
	 * We average out the previous and next data point when generating
	 * the results to be displayed.
	 */
	write_imagef(result, (int2)(X, Y), (od + nd) / 2);
}
