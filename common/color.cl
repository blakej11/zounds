/*
 * color.cl - common image/color manipulation routines.
 *
 * There are three pairs of interfaces offered here. Each pair takes the
 * following form:
 *
 *	static float3
 *	image_to_xxx(
 *		pix_t			X,		// in
 *		pix_t			Y,		// in
 *		__read_only image2d_t	image)		// in
 *
 *	static void
 *	xxx_to_image(
 *		pix_t			X,		// in
 *		pix_t			Y,		// in
 *		float3			xxx,		// in
 *		__write_only image2d_t	image)		// out
 *
 * image_to_xxx() takes an (X,Y) pixel position and a read-only image, and
 * returns the value of that pixel in "xxx-space" (e.g. RGB space).
 * xxx_to_image() takes an (X,Y) pixel position, a write-only image, and a
 * value for that pixel in xxx-space, and updates the image at that pixel
 * with the new value.  In all cases, the xxx-space pixel values
 * returned/provided are assumed to have three coordinates, and each
 * coordinate has a value in [0.0, 1.0].
 *
 * The actual implementations available are:
 *
 * - Hue/Saturation/Value:
 *	float3	image_to_hsv(X, Y, image);
 *	void	hsv_to_image(X, Y, hsv, image);
 *
 * - Luma/Chroma/Hue:
 *	float3	image_to_lch(X, Y, image);
 *	void	lch_to_image(X, Y, lch, image);
 *
 * - Red/Green/Blue:
 *	float3	image_to_rgb(X, Y, image);
 *	void	rgb_to_image(X, Y, rgb, image);
 */

/*
 * ------------------------------------------------------------------
 * HSV - Hue/Saturation/Value.
 */

/*
 * <H,S,V> is in [0,1].
 */
static float3
image_to_hsv(
	pix_t			X,		/* in */
	pix_t			Y,		/* in */
	__read_only image2d_t	image)		/* in */
{
	const int2	coord = (int2)(X, Y);
	const float4	rgb = read_imagef(image, coord);

	/*
	 * Convert <R,G,B> to <H,S,V>.
	 */
	const float	r = rgb.x;
	const float	g = rgb.y;
	const float	b = rgb.z;

#define	MIN(a, b)	(((a) < (b)) ? (a) : (b))
#define	MAX(a, b)	(((a) > (b)) ? (a) : (b))
	const float	min = MIN(MIN(r, g), b);
	const float	max = MAX(MAX(r, g), b);
	const float	delta = max - min;
#undef	MIN
#undef	MAX

	float		h, s, v;

	if (delta < 0.00001 || max == 0.0) {
		s = 0.0;
		h = 0.0;	/* arbitrary */
	} else {
		s = delta / max;

		if (max == r) {
			/* between yellow and magenta */
			h = 0.0 + (g - b) / delta;
		} else if (max == g) {
			/* between cyan and yellow */
			h = 2.0 + (b - r) / delta;
		} else {
			/* between magenta and cyan */
			h = 4.0 + (r - g) / delta;
		}
		if (h < 0.0) {
			h += 6.0;
		}
	}
	v = max;

	return (float3)(h / 6.0f, s, v);
}

/*
 * Inside this algorithm, h needs to be in [0, 6],
 * and s and v need to be in [ 0, 1 ].
 *
 * But the components of "hsv" that's passed in are in the range [0, 1].
 */
static void
hsv_to_image(
	pix_t			X,		/* in */
	pix_t			Y,		/* in */
	float3			hsv,		/* in */
	__write_only image2d_t	image)		/* out */
{
	const float	h = hsv.x * 6.0f;
	const float	s = hsv.y;
	const float	v = hsv.z;

	/*
	 * Convert <H,S,V> to <R,G,B>.
	 */
	const int	w = (int)h;
	const float	f = h - (float)w;
	const float	p = v * (1.0 - s);
	const float	q = v * (1.0 - (s * f));
	const float	t = v * (1.0 - (s * (1.0 - f)));
	float		r, g, b;

	switch (w) {
		case 6:
		case 0: r = v; g = t; b = p; break;
		case 1: r = q; g = v; b = p; break;
		case 2: r = p; g = v; b = t; break;
		case 3: r = p; g = q; b = v; break;
		case 4: r = t; g = p; b = v; break;
		case 5: r = v; g = p; b = q; break;
	}

	const int2	coord = (int2)(X, Y);
	const float4	result = (float4)(r, g, b, 0);
	write_imagef(image, coord, result);
}

/*
 * ------------------------------------------------------------------
 * LCH - Luma/Chroma/Hue.
 */

/* Illuminant D65 - the white point. */
#define	LAB_Xn	0.950470
#define	LAB_Yn	1
#define	LAB_Zn	1.088830

#define	LAB_Kn	18
#define	LAB_t0	0.137931034
#define	LAB_t1	0.206896552
#define	LAB_t2	0.12841855
#define	LAB_t3	0.008856452

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -  */

/* Convert an sRGB value to an RGB value. */
static float
rgb_for_srgb(float r)
{
	if (r <= 0.00304) {
		return (r * 12.92);
	} else {
		return (1.055 * pow(r, 1.0f / 2.4f) - 0.055);
	}
};

static float
xyz_for_lab(float t)
{
	if (t > LAB_t1) {
		return (t * t * t);
	} else {
		return (LAB_t2 * (t - LAB_t0));
	}
};

/* returns RGB values in [0, 1) */
static float3
rgb_for_lab(float3 lab)
{
	const float	L = lab.x;
	const float	A = lab.y;
	const float	B = lab.z;
	float		x, y, z, r, g, b;

	y = (L + 16) / 116;
	x = isnan(A) ? y : y + A / 500;
	z = isnan(B) ? y : y - B / 200;
	y = LAB_Yn * xyz_for_lab(y);
	x = LAB_Xn * xyz_for_lab(x);
	z = LAB_Zn * xyz_for_lab(z);
	r = rgb_for_srgb( 3.2404542 * x - 1.5371385 * y - 0.4985314 * z);
	g = rgb_for_srgb(-0.9692660 * x + 1.8760108 * y + 0.0415560 * z);
	b = rgb_for_srgb( 0.0556434 * x - 0.2040259 * y + 1.0572252 * z);

	return ((float3)(r, g, b));
}

static float3
lab_for_lch(float3 lch)
{
	const float	l = lch.x;
	const float	c = lch.y;
	const float	h = lch.z;

	return ((float3)(l, cospi(h / 180) * c, sinpi(h / 180) * c));
}

/*
 * The range for Luminance and Chroma depend on the hue, but go roughly
 * from 0 to 100-150. The range for Hue is 0 to 360.
 *
 * another article claims: Hue is 0-360, L is 0-100 (balanced at 50),
 * C is 0-100 (grey at 0, sat at 100).
 */
static float3
lch2rgb(float3 lch)
{
	return (rgb_for_lab(lab_for_lch(lch)));
}

static void
lch_to_image(pix_t X, pix_t Y, float3 lch, __write_only image2d_t image)
{
	const float	L = lch.z * 100;
	const float	C = lch.y * 100;
	const float	H = lch.x * 360;

	const float3	rgb = lch2rgb((float3)(L, C, H));

	write_imagef(image, (int2)(X, Y), (float4)(rgb, 0));
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -  */

/* Convert an RGB value to an sRGB value, by doing gamma correction. */
static float
srgb_for_rgb(float r)
{
	if (r <= 0.04045) {
		return (r / 12.92);
	} else {
		return (pow((r + 0.055f) / 1.055f, 2.4f));
	}
};

/* Convert an XYZ-scale value to a Lab-scale value. */
static float
lab_for_xyz(float t)
{
	if (t > LAB_t3) {
		return (pow(t, 1 / 3));
	} else {
		return (t / LAB_t2 + LAB_t0);
	}
};

/* http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_RGB.html */
static float3
lab_for_rgb(float3 rgb)
{
	const float	sr = srgb_for_rgb(rgb.x);
	const float	sg = srgb_for_rgb(rgb.y);
	const float	sb = srgb_for_rgb(rgb.z);

	/* Multiply by the RGB-to-XYZ conversion matrix for sRGB. */
	const float	Mx =
	    (0.4124564 * sr + 0.3575761 * sg + 0.1804375 * sb) / LAB_Xn;
	const float	My =
	    (0.2126729 * sr + 0.7151522 * sg + 0.0721750 * sb) / LAB_Yn;
	const float	Mz =
	    (0.0193339 * sr + 0.1191920 * sg + 0.9503041 * sb) / LAB_Zn;

	/* Convert the multiplied values to Lab-scale values. */
	const float	X = lab_for_xyz(Mx);
	const float	Y = lab_for_xyz(My);
	const float	Z = lab_for_xyz(Mz);

	/* Convert XYZ to Lab. */
	return ((float3)(116 * Y - 16, 500 * (X - Y), 200 * (Y - Z)));
};

static float3
lch_for_lab(float3 lab)
{
	const float	l = lab.x;
	const float	a = lab.y;
	const float	b = lab.z;
	float		c, h;

	c = length((float2)(a, b));
	h = fmod(atan2pi(b, a) + 2, 2) * 180.0f;
	if (round(c * 10000) == 0) {
		h = nan((uint)0);
	}
	return ((float3)(l, c, h));
}

/* this takes RGB in [0, 1), not [0, 256) */
static float3
rgb2lch(float3 rgb)
{
	return (lch_for_lab(lab_for_rgb(rgb)));
}

static float3
image_to_lch(pix_t X, pix_t Y, __read_only image2d_t image)
{
	const float3	rgb = as_float3(read_imagef(image, (int2)(X, Y)));

	return (rgb2lch(rgb));
}

/*
 * ------------------------------------------------------------------
 * RGB - Red/Green/Blue.
 */

static void
rgb_to_image(
	pix_t			X,		/* in */
	pix_t			Y,		/* in */
	float3			rgb,		/* in */
	__write_only image2d_t	image)		/* out */
{
	write_imagef(image, (int2)(X, Y), (float4)(rgb, 0));
}

static float3
image_to_rgb(
	pix_t			X,		/* in */
	pix_t			Y,		/* in */
	__read_only image2d_t	image)		/* in */
{
	return (as_float3(read_imagef(image, (int2)(X, Y))));
}

/* ------------------------------------------------------------------ */

/*
 * This is just to quiet the compiler in the case where one or more of
 * these routines aren't used.
 */
__kernel void
_ignore_me_(
	__read_only image2d_t	src,
	__write_only image2d_t	dst)
{
	const pix_t	X = get_global_id(0);
	const pix_t	Y = get_global_id(1);
	rgb_to_image(X, Y, image_to_rgb(X, Y, src), dst);
	hsv_to_image(X, Y, image_to_hsv(X, Y, src), dst);
	lch_to_image(X, Y, image_to_lch(X, Y, src), dst);
}
