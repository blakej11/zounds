/*
 * core.h - interface for a core algorithm.
 */

#ifndef	_CORE_H
#define	_CORE_H

#include "types.h"

/*
 * The operations provided by a core algorithm.
 */
typedef struct {
	/*
	 * Convert an RGBA image2d_t into an image2d_t made of datavec's.
	 * A datavec is an N-dimensional vector of data (where "N" =
	 * DATA_DIMENSIONS, as defined by vecsizes.h in the core algorithm's
	 * directory).
	 *
	 * RGBA values are in the range [ 0.0, 1.0 ].  The "A" channel doesn't
	 * contain valid data and should be ignored; it is included here
	 * because OpenCL represents three-dimensional vectors using four data
	 * elements.
	 *
	 * Data values are in the range [ (*min)(), (*max)() ].
	 * If N>1, and the vectors fit inside the N-sphere of radius 1,
	 * (*datavec_shape)() should return DATAVEC_SHAPE_SPHERE; otherwise
	 * it should return DATAVEC_SHAPE_CUBE.
	 */
	void	(*unrender)(cl_mem image, cl_mem data);

	/*
	 * Given an image2d_t of datavecs as described above, initialize the
	 * internal state of the core algorithm so it operates on that data in
	 * its next pass.
	 */
	void	(*import)(cl_mem data);

	/*
	 * Advance the state of the core algorithm by one step.  After doing
	 * so, use its internal state to generate an image2d_t of datavecs.
	 */
	void	(*step_and_export)(cl_mem data);

	/*
	 * Given an image2d_t of datavecs, create the corresponding image in
	 * an image2d_t with RGBA components.
	 *
	 * Data values and RGBA values are as described in unrender(), above.
	 */
	void	(*render)(cl_mem data, cl_mem image);

	/* The minimum value of any component of a data vector. */
	float	(*min)(void);

	/* The maximum value of any component of a data vector. */
	float	(*max)(void);

	/* Whether datavec's fit into a sphere or a cube. */
	datavec_shape_t (*datavec_shape)(void);
} core_ops_t;

/*
 * Register the specified set of operations.
 */
extern void
core_ops_register(core_ops_t *ops);

/*
 * Unregister the specified set of operations.
 */
extern void
core_ops_unregister(core_ops_t *ops);

#endif	/* _CORE_H */
