/*
 * stroke.cl - a computational kernel for generating ink-marbling effects.
 */

/*
 * Generate a "stroke" in the image, akin to moving a wand through a
 * tank of multiply colored ink.
 *
 * The algorithm is taken directly from Aubrey Jaffer's paper
 * "Oseen Flow in Ink Marbling": https://arxiv.org/pdf/1702.02106v1.pdf
 *
 * This helper function implements the core of the algorithm; it's called
 * from stroke(), below, which normalizes the coordinates and does the
 * actual image movement.
 *
 * The names of the variables, and the operation of the algorithm,
 * are taken directly from the paper; I won't try to summarize it.
 */
static float2
stroke_delta(
	const float2		P,		/* in: current pixel */
	const float2		B,		/* in: stroke start */
	const float2		E,		/* in: stroke end */
	const float		L)		/* in: viscosity parameter */
{
	const float	lambda = length(E - B);		// length of movement
	const float2	N = (E - B) / lambda;		// direction of mvt

#if 0
	/* Equation 6 from section 8 of the paper. */
	const float2	PB = P - B;			// dir to pt from mvt
	const float	r = length(PB);
	const float	x = (N.x * PB.x + N.y * PB.y);	// dot(N, PB);
	const float	y = (N.x * PB.y - N.y * PB.x);	// cross(N, PB);
	const float	df = lambda * exp(-r / L) / (r * L);
	const float2	delta = df * (float2)(r * L - y * y, x * y);
#else
	/* Equation 14 from section 8 of the paper. */
	const float2	C = (B + E) / 2;
	const float2	PB = P - B;
	const float2	PC = P - C;
	const float2	PE = P - E;
	const float	xB = dot(N, PB);
	const float	xE = dot(N, PE);
	const float	y = (N.x * PC.y - N.y * PC.x);	// cross(N, PC);
	const float	r = length(PB);
	const float	s = length(PE);
	const float	d1f = lambda * exp(-r / L) / (2 * r * L);
	const float2	d1 = d1f * (float2)(r * L - y * y, xB * y);
	const float	d2f = lambda * exp(-s / L) / (2 * s * L);
	const float2	d2 = d2f * (float2)(s * L - y * y, xE * y);
	const float2	delta = d1 + d2;
#endif

	/* The final rotational transform, common to both variants. */
	const float2	Delta = (float2)(
	    N.x * delta.x - N.y * delta.y,
	    N.y * delta.x + N.x * delta.y);

	return (Delta);
}

/*
 * The code invoking this kernel is responsible for subdividing long strokes
 * into shorter ones, as recommended in the paper.
 */
__kernel void
stroke(
	const pix_t		W,		/* in: width of image */
	const pix_t		H,		/* in: height of image */
	const pix_t		Bx,		/* in: stroke start pos., X */
	const pix_t		By,		/* in: stroke start pos., Y */
	const pix_t		Ex,		/* in: stroke end position, X */
	const pix_t		Ey,		/* in: stroke end position, Y */
	const float		L,		/* in: viscosity parameter */
	__read_only image2d_t	image,		/* in: source image */
	__write_only image2d_t	dst)		/* out: updated image */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);

	if (X >= W || Y >= H) {
		return;
	}

	/*
	 * Convert from absolute coordinates to normalized coordinates.
	 * The integer location of the pixel is in its upper left-hand
	 * corner, so adding 0.5 to it puts us right in the middle of it.
	 */
#define	NORM(x,y)	\
	(float2)(((float)(x) + 0.5) / (float)W, ((float)(y) + 0.5) / (float)H)

	/*
	 * The names of these variables are taken directly from the paper.
	 */
	const float2	P = NORM(X, Y);
	const float2	B = NORM(Bx, By);
	const float2	E = NORM(Ex, Ey);

	/*
	 * If we only use the point "P" for calculating stroke behavior,
	 * none of the strokes "push" their contents across the edges of
	 * the screen.  This leads to weird asymmetries, since the core
	 * algorithms assume that the image wraps on all sides.
	 *
	 * So, since GPUs are ridiculously fast at doing math, we consider
	 * nine copies of the image (the original, plus copies arranged on
	 * all edges and corners), and calculate the delta that the point
	 * would get if the stroke was actually happening in each respective
	 * copy.  The vast majority of the movement that a point is going
	 * to undergo is going to come from just one copy, so adding them
	 * together is a straightforward way to combine all of the effects.
	 */
	const float2	Delta = \
		stroke_delta(P, B + (float2)(-1, -1), E + (float2)(-1, -1), L) +
		stroke_delta(P, B + (float2)(-1,  0), E + (float2)(-1,  0), L) +
		stroke_delta(P, B + (float2)(-1,  1), E + (float2)(-1,  1), L) +
		stroke_delta(P, B + (float2)( 0, -1), E + (float2)( 0, -1), L) +
		stroke_delta(P, B + (float2)( 0,  0), E + (float2)( 0,  0), L) +
		stroke_delta(P, B + (float2)( 0,  1), E + (float2)( 0,  1), L) +
		stroke_delta(P, B + (float2)( 1, -1), E + (float2)( 1, -1), L) +
		stroke_delta(P, B + (float2)( 1,  0), E + (float2)( 1,  0), L) +
		stroke_delta(P, B + (float2)( 1,  1), E + (float2)( 1,  1), L);

	/*
	 * The point of this whole operation is to move the contents of
	 * location P to location P + Delta.  But P + Delta is (probably) not
	 * on an exact pixel boundary; we can't store to a linearly
	 * interpolated address, and we definitely can't do such a store in a
	 * way that would play nice with the parallelism that GPUs provide.
	 * So instead, as an approximation, we move the contents of location
	 * P - Delta *to* location P.
	 */
	const float2	srccoord = P - Delta;
	const sampler_t	sampler = CLK_NORMALIZED_COORDS_TRUE |
		CLK_ADDRESS_REPEAT | CLK_FILTER_LINEAR;

	/* Destination coordinates are not normalized. */
	const int2	dstcoord = (int2)(X, Y);

	/* Here's where the data gets moved. */
	write_imagef(dst, dstcoord, read_imagef(image, sampler, srccoord));
}
