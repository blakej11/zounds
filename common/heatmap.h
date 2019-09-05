/*
 * heatmap.h - interfaces for generating a heatmap image of data.
 */

#ifndef	_HEATMAP_H
#define	_HEATMAP_H

#include "types.h"

/*
 * Update "image" with a heatmap image of the data in "data".  min/max/shape
 * are given by the core algorithm, and describe the shape of the data.
 */
extern void
heatmap_update(cl_mem data, float min, float max, datavec_shape_t shape,
    cl_mem image);

#endif	/* _HEATMAP_H */
