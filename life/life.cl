/*
 * life.cl - the computational kernels for John Conway's Game of Life.
 */

#define	UNIT		(0.01f)

#define	WRAP(x,max)	(((x) + (max)) % (max))
#define	PIXEL(x,y,w,h)	(((WRAP(y,h)) * (w)) + (WRAP(x,w)))

/* ------------------------------------------------------------------ */

/*
 * This performs the inverse of the value-to-color mapping defined in render().
 */
__kernel void
unrender(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	const float		thresh,		/* in */
	__read_only image2d_t	image,		/* in */
	__write_only image2d_t	data)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);
	float		val;

	if (X < W && Y < H) {
		const float3	hsv = image_to_hsv(X, Y, image);

		if (hsv.z < thresh) {
			val = hsv.z;
		} else {
			const float	a = 1 - fmod(hsv.x / 0.7f, 1.0f);
			val = a * (1 - thresh) + thresh;
		}

		/* Data and internal representation are in [0, 1]. */
		write_imagef(data, (int2)(X, Y), val);
	}
}

__kernel void
import(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	const float		thresh,		/* in */
	__read_only image2d_t	src,		/* in */
	__global float		*dst)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);

	if (X < W && Y < H) {
		dst[Y * W + X] = as_datavec(read_imagef(src, (int2)(X, Y)));
	}
}

/* ------------------------------------------------------------------ */

/*
 * from http://http.developer.nvidia.com/GPUGems3/gpugems3_ch37.html, via
 * https://math.stackexchange.com/questions/337782/pseudo-random-number-generation-on-the-gpu
 *
 * This is overkill for the current task, but simpler PRNGs that didn't keep
 * per-pixel state showed very obvious periodicity.
 */

static uint
LCGStep(uint z, uint A, uint C)
{
	return (A * z + C);    
}

static uint
TausStep(uint z, int S1, int S2, int S3, uint M)
{
	return ((((z << S1) ^ z) >> S2) ^ ((z & M) << S3));
}

static float
generate_random(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	__global uint4		*random_state)	/* in/out */
{
	const pix_t	pix = get_global_id(1) * W + get_global_id(0);
	uint4		state;

	state = random_state[pix];
	state.w = LCGStep(state.w, 1664525, 1013904223);	// p = 2^32
	state.x = TausStep(state.x, 13, 19, 12, (uint)( -2));	// p = 2^31 - 1
	state.y = TausStep(state.y,  2, 25,  4, (uint)( -8));	// p = 2^30 - 1
	state.z = TausStep(state.z,  3, 11, 17, (uint)(-16));	// p = 2^28 - 1
	random_state[pix] = state;

	return (2.3283064365387e-10 * (state.x ^ state.y ^ state.z ^ state.w));
}

/* ------------------------------------------------------------------ */

__kernel void
step_and_export(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	const float		thresh,		/* in */
	const int		steps,		/* in */
	__global uint4		*random_state,	/* in/out */
	__global float		*src,		/* in */
	__global float		*dst,		/* out */
	__write_only image2d_t	result)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);
	const pix_t	pix = Y * W + X;
	spix_t		x, y;

	if (X >= W || Y >= H) {
		return;
	}

	const float	ov = src[pix];
	float		nv;

#define	IS_ALIVE(v)	((v) > (thresh))

	/*
	 * Very standard Game of Life algorithm.
	 */
	int	count = 0;
	for (y = Y - 1; y <= (spix_t)Y + 1; y++) {
		for (x = X - 1; x <= (spix_t)X + 1; x++) {
			count += IS_ALIVE(src[PIXEL(x, y, W, H)]);
		}
	}
	count -= IS_ALIVE(ov);

	if (IS_ALIVE(ov) && (count == 2 || count == 3)) {
		/*
		 * Keep it alive, but "age" the pixel one step (until it gets
		 * to the minimum allowed age that's still alive).
		 */
		if (IS_ALIVE(ov - UNIT)) {
			nv = ov - UNIT;
		} else {
			nv = ov;
		}
	} else if (!IS_ALIVE(ov) && count == 3) {
		/*
		 * Make it fully alive.
		 */
		nv = 1.0f;
	} else {
		/*
		 * It's not alive; give it a new random sub-threshold value.
		 * This allows random new growth to happen if the threshold
		 * is reduced.
		 */
		nv = generate_random(W, H, random_state) * thresh;
	}

	dst[pix] = nv;

	write_imagef(result, (int2)(X, Y), nv);
}

__kernel void
render(
	pix_t			W,		/* in */
	pix_t			H,		/* in */
	const float		thresh,		/* in */
	__read_only image2d_t	data,		/* in */
	__write_only image2d_t	image)		/* out */
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);

	if (X < W && Y < H) {
		const float	d =
		    as_datavec(read_imagef(data, (int2)(X, Y)));
		float3		hsv;

		if (d < thresh) {
			hsv = (float3)(0, 0, 0);
		} else {
			/*
			 * This makes new pixels red; as they age, they
			 * follow the hues around through warm to cool.
			 */
			const float	a = (d - thresh) / (1 - thresh);
			const float	b = (1 - a) * 0.7f;
			hsv = (float3)(b, 1, 1);
		}

		hsv_to_image(X, Y, hsv, image);
	}
}
