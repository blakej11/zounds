/*
 * basis.c - code for calculating a pair of basis vectors, for projecting
 * the dataset onto 2-space.
 *
 * The goal is to be able to look at a multi-dimensional dataset in 2-D
 * in some interesting way with minimal interactive effort.  The first attempt
 * at this just cycled through pairs of axes in the dataset, mapping the data
 * from those axes onto the X-Y plane of the display.  But the jump between
 * axis pairs can be pretty sudden, and at least initially this jump was done
 * via keystroke.
 *
 * The key observation in making this more interesting is that the operation of
 * "map data from a pair of axes onto the X-Y plane" can be generalized to
 * "take the dot product of each data point with a pair of orthogonal (basis)
 * vectors".  These vectors don't form a basis of the data's own N-space, but
 * they create a basis for the projected 2-space.  The first dot product yields
 * the X-axis value, and the second yields the Y-axis value.  As long as these
 * two vectors are kept orthogonal, they can move smoothly around N-space,
 * giving a smoother fly-around view of the dataset.
 *
 * The basis vectors have (2 * N - 3) free parameters.  In 2-D, there is just
 * one parameter - the angle (theta) of the first basis vector.  The second
 * vector is pi/2 radians away from it.  In 3-D, there are three parameters: 
 * theta_1 and phi define one 3-vector, while the other 3-vector is defined by
 * a theta_2 but has its own phi which forces it to be orthogonal to the first.
 *
 * So, this code consists of two parts:
 *
 * - Code to generate and smoothly vary the free parameters.
 *
 * - A function which calculates the actual basis vectors given the free
 *   parameters.  This function (basis_generate()) has a different definition
 *   depending on how many dimensions the data is.  
 *
 * Open issues:
 *
 * - This still isn't all that smooth; changes to some angles have much more
 *   impact on the perceived "motion" than others do.  For example, rotating
 *   a vector through 180 degrees of longitude covers much more ground at the
 *   equator than at 85 degrees North.
 */

#include <math.h>
#include <strings.h>

#include "basis.h"
#include "common.h"
#include "debug.h"
#include "keyboard.h"
#include "module.h"
#include "param.h"
#include "randbj.h"

#if	DATA_DIMENSIONS == 1

/*
 * Heatmaps just don't work with one-dimensional data.  So here is a
 * stub symbol to allow this to compile in 1-D.
 */
const module_ops_t	basis_ops = { };

#else	/* DATA_DIMENSIONS > 1 */

/* ------------------------------------------------------------------ */

/*
 * A data structure describing one free parameter.  "angle" and "max" are
 * multiplied by pi before being used.
 */
typedef struct {
	float		angle;	/* current value of this angle */
	float		max;	/* max value for this angle */

	float		scale;	/* scaling rate */

	int		sign;	/* +1 or -1 */
	float		x;	/* total amount to change by */
	float		v;	/* current rate of change */
	float		vtgt;	/* target rate of change */
	float		vmax;	/* max rate of change */
	float		a;	/* current acceleration */
	float		amax;	/* max acceleration */
} angle_t;

#define	NANGLES	((DATA_DIMENSIONS * 2) - 3)

static struct {
	angle_t		angles[NANGLES];

	param_id_t	id;	/* parameter ID for spin rate */
	int		ovalue;	/* old spin rate */
} Basis;

/* ------------------------------------------------------------------ */

#if	DATA_DIMENSIONS == 2	/* ---------------------------------- */

/*
 * How to project a datavec onto 2-space.  This describes one basis vector
 * using polar coordinates; the second one is pi/2 ahead of it.
 */
#define	ANGLE_THETA	0

static bool
basis_generate(cl_datavec bases[2])
{
	const float	theta = Basis.angles[ANGLE_THETA].angle * M_PI;
	const float	ct = cosf(theta);
	const float	st = sinf(theta);

	/*
	 * Standard polar coordinates.
	 */
	bases[0].s[0] = ct;
	bases[0].s[1] = st;

	/*
	 * A vector normal to bases[0].
	 */
	bases[1].s[0] = -st;
	bases[1].s[1] = ct;
}

#elif	DATA_DIMENSIONS == 3	/* ---------------------------------- */

/*
 * How to project a 3-D datavec onto 2-space.  theta1 and phi describe a
 * unit 3-vector using spherical coordinates.  To construct the basis for
 * the projection, we use theta2 to choose another unit vector in the plane
 * normal to the first vector.
 */
#define	ANGLE_THETA1	0
#define	ANGLE_PHI	1
#define	ANGLE_THETA2	2

static bool
basis_generate(cl_datavec bases[2])
{
	const float	t1 = Basis.angles[ANGLE_THETA1].angle * M_PI;
	const float	phi = Basis.angles[ANGLE_PHI].angle * M_PI;
	const float	t2 = Basis.angles[ANGLE_THETA2].angle * M_PI;

	const float	sp = sinf(phi);
	const float	cp = cosf(phi);
	const float	c12 = cosf(t1 - t2);
	const float	K = sqrtf(cp * cp + sp * sp * c12 * c12);

	if (K == 0) {
		return (false);	/* degenerate result, try again */
	}

	/*
	 * Standard spherical coordinates.
	 */
	bases[0].s[0] = sp * cosf(t1);
	bases[0].s[1] = sp * sinf(t1);
	bases[0].s[2] = cp;

	/*
	 * A vector in the plane normal to bases[0].
	 */
	bases[1].s[0] = -cp * cosf(t2) / K;
	bases[1].s[1] = -cp * sinf(t2) / K;
	bases[1].s[2] =  sp * c12 / K;

	return (true);
}

#elif	DATA_DIMENSIONS == 4	/* ---------------------------------- */

/*
 * How to project a 4-D datavec onto 2-space.  theta1, theta2, and phi1
 * describe a unit 4-vector using hyperspherical coordinates.  To construct
 * the basis for the projection, we use theta3 and phi2 to pick another
 * unit vector in the 3-space normal to the first vector.
 */
#define	ANGLE_THETA1	0
#define	ANGLE_THETA2	1
#define	ANGLE_THETA3	2
#define	ANGLE_PHI1	3
#define	ANGLE_PHI2	4

static bool
basis_generate(cl_datavec bases[2])
{
	const float	theta1 = Basis.angles[ANGLE_THETA1].angle * M_PI;
	const float	theta2 = Basis.angles[ANGLE_THETA2].angle * M_PI;
	const float	theta3 = Basis.angles[ANGLE_THETA3].angle * M_PI;
	const float	phi1 = Basis.angles[ANGLE_PHI1].angle * M_PI;
	const float	phi2 = Basis.angles[ANGLE_PHI2].angle * M_PI;

	const float	s1 = sinf(theta1);
	const float	c1 = cosf(theta1);
	const float	s2 = sinf(theta2);
	const float	c2 = cosf(theta2);
	const float	s3 = sinf(theta3);
	const float	c3 = cosf(theta3);
	const float	sp = sinf(phi1);
	const float	cp = cosf(phi1);
	const float	sq = sinf(phi2);
	const float	cq = cosf(phi2);
	const float	cpq = cosf(phi1 - phi2);

	const float	tK = s1 * (c2 * c3 + s2 * s3 * cpq);
	const float	K = sqrtf(c1 * c1 + tK * tK);

	if (K == 0) {
		return (false);	/* degenerate result, try again */
	}

	/*
	 * Standard hyperspherical coordinates.
	 */
	bases[0].s[0] = c1;
	bases[0].s[1] = s1 * c2;
	bases[0].s[2] = s1 * s2 * cp;
	bases[0].s[3] = s1 * s2 * sp;

	/*
	 * A vector in the 3-space normal to bases[0].
	 */
	bases[1].s[0] =  tK / K;
	bases[1].s[1] = -c1 * c3 / K;
	bases[1].s[2] = -c1 * s3 * cq / K;
	bases[1].s[3] = -c1 * s3 * sq / K;

	return (true);
}

#endif	/* ---------------------------------------------------------- */

/* ------------------------------------------------------------------ */

/*
 * This is called when the basis vector rotation rate parameter is adjusted.
 */
static void
basis_adjust(void)
{
	const int	nval = param_int(Basis.id);

	for (int i = 0; i < NANGLES; i++) {
		if (nval < 0) {
			Basis.angles[i].scale = 0.0f;
		} else {
			Basis.angles[i].scale = (float)(1 << nval);
		}
	}
}

static void
basis_preinit(void)
{
	param_init_t	pi;
	int		i;

	for (i = 0; i < NANGLES; i++) {
		angle_t	*a = &Basis.angles[i];

		/*
		 * Even though theta's in (hyper-)spherical coordinates
		 * traditionally range from [0, pi], the display gets a jarring
		 * discontinuity when one crosses from 0 to pi or vice-versa.
		 * So since this is going for a pretty display rather than a
		 * maximally compact representation of N-space, I'm just going
		 * with a range of [0, 2*pi] for all of them.
		 */
		a->max = 2.0;
		a->angle = fmodf(drandbj(), a->max);

		/*
		 * The maximum velocity and acceleration are scaled by the
		 * parameter below.
		 */
		a->vmax = 1 / 2000.0f;
		a->amax = 1 / 20000.0f;
	}

	/*
	 * Parameter for tweaking the maximum velocity of any angle.
	 */
	bzero(&pi, sizeof (pi));
	pi.pi_min = -1;			/*  0 * pi/2000 per step */
	pi.pi_default = 1;		/*  2 * pi/2000 per step */
	pi.pi_max = 6;			/* 64 * pi/2000 per step */
	pi.pi_units = 1;
	pi.pi_ap_freq = AP_FREQ_OFF;
	pi.pi_ap_rate = AP_RATE_OFF;

	Basis.id = param_register("basis vector rotation rate", &pi);
	Basis.ovalue = param_int(Basis.id);

	param_key_register('x', KB_DEFAULT, Basis.id, -1);
	param_key_register('X', KB_DEFAULT, Basis.id,  1);

	param_cb_register(Basis.id, basis_adjust);
}

const module_ops_t	basis_ops = {
	basis_preinit
};

/* ------------------------------------------------------------------ */

static void
basis_pivot(angle_t *a, int i)
{
	/*
	 * If we're done with the most recent motion of this angle,
	 * pick new parameters for its next step.
	 */
	if (a->x <= 0.0) {
		a->x = fmodf(drandbj(), a->max);

		a->sign = (drandbj() >= 0.5 ? 1 : -1);
		a->v = 0.0;
		a->vtgt = fmod(drandbj(), a->vmax);
		a->a = fmod(drandbj(), a->amax);
		verbose(0, "Resetting angle %d: x=% .5f, vt=% .5f, a=% .5f\n",
		    i, a->x, a->vtgt, a->a);
	}

	/*
	 * Decide how fast to move on this step.
	 */
	if (a->v < a->vtgt) {
		/* If not at the target velocity, speed up. */
		a->v = MIN(a->v + a->a, a->vtgt);
	} else if (a->x < (a->v * a->v * a->scale) / (a->a * 2.0)) {
		/* If nearing the end, slow down. */
		a->v = MAX(a->v - a->a, 0.0);
	}

	/*
	 * Adjust the angle, and track the movement in a->x.
	 */
	a->angle = fmod(a->angle + a->v * a->scale * a->sign + a->max, a->max);
	a->x -= a->v;
}

/*
 * Pivot the angles used to construct the projection basis, and then
 * generate basis vectors for projecting the data onto 2-space.
 */
void
basis_update(cl_datavec bases[2])
{
	int	i, j, v;
	bool	rv;

	do {
		for (int i = 0; i < NANGLES; i++) {
			basis_pivot(&Basis.angles[i], i);
		}
		rv = basis_generate(bases);
	} while (rv == false);

	/*
	 * The rest is just debugging output.
	 */
	if (!debug_enabled(DB_HEAT)) {
		return;
	}

	/* Display the basis vectors. */
	debug(DB_HEAT, "Heatmap:");
	for (v = 0; v < 2; v++) {
		debug(DB_HEAT, " ");
		for (j = 0; j < DATA_DIMENSIONS; j++) {
			debug(DB_HEAT, "%s% 5.3f%c",
			    (j == 0 ? "<" : ""), bases[v].s[j],
			    (j == DATA_DIMENSIONS - 1 ? '>' : ','));
		}
		debug(DB_HEAT, (v == 0 ? "," : "\n"));
	}

	/* Make sure the basis vectors are orthogonal and unit-length. */
	float dot, s0, s1;
	dot = s0 = s1 = 0;
	for (j = 0; j < DATA_DIMENSIONS; j++) {
		const float b0 = bases[0].s[j];
		const float b1 = bases[1].s[j];
		dot += b0 * b1;
		s0 += b0 * b0;
		s1 += b1 * b1;
	}
	s0 -= 1.0f;
	s1 -= 1.0f;
	debug(DB_HEAT, "\tdot, xlen, ylen: [");
	debug(DB_HEAT, " %5.2f", -logf(fabsf(dot)));	/* inf -> dot  = 0 */
	debug(DB_HEAT, " %5.2f", -logf(fabsf(s0 )));	/* inf -> |b0| = 1 */
	debug(DB_HEAT, " %5.2f", -logf(fabsf(s1 )));	/* inf -> |b1| = 1 */
	debug(DB_HEAT, " ]\n");

	/* Display the angles. */
	debug(DB_HEAT, "\tangles:          [");
	for (v = 0; v < NANGLES; v++) {
		angle_t	*a = &Basis.angles[v];
		debug(DB_HEAT, " % .5f", a->angle);
	}
	debug(DB_HEAT, " ]\n");
}

#endif	/* DATA_DIMENSIONS > 1 */
