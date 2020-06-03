/*
 * randbj.h - pseudo-random numbers.
 * These should have the same semantics as srand48() / drand48() / lrand48().
 */

#ifndef	_RANDBJ_H
#define	_RANDBJ_H

/*
 * Set the random number generation seed to the specified value.
 */
extern void
srandbj(int seed);

/*
 * Get a pseudo-random floating point number in the range [0.0, 1.0).
 */
extern double
drandbj(void);

/*
 * Get a pseudo-random long integer in the range [0, 2^31).
 */
extern long
lrandbj(void);

/*
 * Get a pseudo-random floating point number that follows a
 * normal distribution with mean 0 and standard deviation 1.
 */
extern double
normrandbj(void);

#endif	/* _RANDBJ_H */
