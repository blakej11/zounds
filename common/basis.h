/*
 * basis.h - interfaces for calculating a pair of basis vectors.
 */

#ifndef	_BASIS_H
#define	_BASIS_H

#include "types.h"

/*
 * Generate new basis vectors for projecting the dataset onto 2-space.
 */
extern void
basis_update(cl_datavec bases[2]);

#endif	/* _BASIS_H */

