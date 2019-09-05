/*
 * heatmap.h - interfaces for generating an ASCII-art histogram of the data.
 */

#ifndef	_HISTOGRAM_H
#define	_HISTOGRAM_H

#include "types.h"

/*
 * Display an ASCII-art histogram of the data in "buf".
 * "min" and "max" are given by the core algorithm.
 */
extern void
histogram_display(cl_mem buf, float min, float max);

#endif	/* _HISTOGRAM_H */
