/*
 * reduce.h - manages image reduction.
 */

#ifndef	_REDUCE_H
#define	_REDUCE_H

#include "types.h"

/*
 * Wrapper around the OpenCL kernel to do image reduction.  This routine
 * just adds up the pixel values and scales them so they are within a [0,
 * 256) range per pixel. This is the better routine for finding totals.
 *
 * - "data" is an image2d_t made of datavec's.
 * - "dim" is which dimension of the datavec to do the reduction upon.
 *   (Right now it only does one at a time.)
 * - "min" and "max" are the bounds of possible data values in the datavec.
 *
 * - "tgtbuffer" is a buffer of "bufedge"x"bufedge" int's, which will hold
 *   the reduced result.
 */
void
reduce_addup(cl_mem data, int dim, float min, float max,
    int *tgtbuffer, pix_t bufedge);

/*
 * This routine invokes reduce_addup(), then scales down each reduced
 * pixel value by the number of original pixels each scaled pixel
 * represents. This is the better routine for finding average values.
 */
void
reduce(cl_mem data, int dim, float min, float max,
    int *tgtbuffer, pix_t bufedge);

#endif	/* _REDUCE_H */
